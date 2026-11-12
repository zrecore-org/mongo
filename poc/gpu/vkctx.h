// Minimal Vulkan compute harness for the GPU proofs of concept.
// The Vulkan loader is opened with dlopen (no link-time dependency), so a missing or broken loader
// makes init() return false and callers fall back to the CPU path.
#pragma once

#include <volk.h>

#include <dlfcn.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace gpu {

inline double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

#define VK_CHECK(x)                                                                          \
    do {                                                                                     \
        VkResult r_ = (x);                                                                   \
        if (r_ != VK_SUCCESS)                                                                \
            throw std::runtime_error(std::string(#x) + " failed: " + std::to_string(r_));    \
    } while (0)

// A buffer. On unified-memory devices (Apple, integrated GPUs) there is one host-visible buffer. On a discrete
// GPU `buf`/`ptr` is a host-visible staging buffer and `dbuf` is its device-local twin; shaders bind the twin
// and Ctx::upload/download record the PCIe copies, so transfer cost is inside the timed submission.
struct Buf {
    VkBuffer buf{};
    VkDeviceMemory mem{};
    void* ptr{};
    size_t size{};
    VkBuffer dbuf{};
    VkDeviceMemory dmem{};
    bool imported{false};   // host memory imported with VK_EXT_external_memory_host (not mapped by us)
    VkBuffer bound() const { return dbuf ? dbuf : buf; }
};

struct Kernel {
    VkShaderModule mod{};
    VkDescriptorSetLayout dsl{};
    VkPipelineLayout pl{};
    VkPipeline pipe{};
    uint32_t nbufs{};
    uint32_t pushBytes{};
    double createMs{};
};

struct Ctx {
    VkInstance inst{};
    VkPhysicalDevice phys{};
    VkDevice dev{};
    VkQueue queue{};
    uint32_t qfam{};
    VkCommandPool cmdPool{};
    VkCommandBuffer cmd{};
    VkDescriptorPool descPool{};
    VkFence fence{};
    VkPhysicalDeviceProperties props{};
    VkPhysicalDeviceFeatures feats{};
    uint32_t memType{};
    bool unifiedMemory{false};
    uint32_t devMemType{};
    bool haveDevMem{false};
    bool hostImport{false};   // VK_EXT_external_memory_host is enabled
    std::string error;

    const char* memMode() const { return unifiedMemory ? "unified" : haveDevMem ? "discrete (device-local + staging copies)" : "host-visible only"; }

    // Returns false (and sets `error`) when no usable Vulkan device exists.
    bool init(const std::vector<std::string>& loaderPaths) {
        try {
            void* h = nullptr;
            for (auto& p : loaderPaths)
                if ((h = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL)))
                    break;
            if (!h)
                throw std::runtime_error("no Vulkan loader found");
            auto gipa = (PFN_vkGetInstanceProcAddr)dlsym(h, "vkGetInstanceProcAddr");
            if (!gipa)
                throw std::runtime_error("vkGetInstanceProcAddr missing");
            volkInitializeCustom(gipa);

            uint32_t n = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
            std::vector<VkExtensionProperties> ie(n);
            vkEnumerateInstanceExtensionProperties(nullptr, &n, ie.data());
            std::vector<const char*> iext;
            VkInstanceCreateFlags iflags = 0;
            for (auto& e : ie)
                if (!strcmp(e.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
                    iext.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                    iflags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
                }
            VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            ai.apiVersion = VK_API_VERSION_1_2;
            ai.pApplicationName = "mongo-gpu-poc";
            VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            ici.flags = iflags;
            ici.pApplicationInfo = &ai;
            ici.enabledExtensionCount = (uint32_t)iext.size();
            ici.ppEnabledExtensionNames = iext.data();
            VK_CHECK(vkCreateInstance(&ici, nullptr, &inst));
            volkLoadInstance(inst);

            uint32_t np = 0;
            VK_CHECK(vkEnumeratePhysicalDevices(inst, &np, nullptr));
            if (!np)
                throw std::runtime_error("no physical devices");
            std::vector<VkPhysicalDevice> pds(np);
            VK_CHECK(vkEnumeratePhysicalDevices(inst, &np, pds.data()));
            phys = pds[0];
            for (auto d : pds) {  // prefer a discrete GPU
                VkPhysicalDeviceProperties p;
                vkGetPhysicalDeviceProperties(d, &p);
                if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    phys = d;
                    break;
                }
            }
            vkGetPhysicalDeviceProperties(phys, &props);
            vkGetPhysicalDeviceFeatures(phys, &feats);

            uint32_t nq = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, nullptr);
            std::vector<VkQueueFamilyProperties> qf(nq);
            vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, qf.data());
            bool found = false;
            for (uint32_t i = 0; i < nq; i++)
                if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    qfam = i;
                    found = true;
                    break;
                }
            if (!found)
                throw std::runtime_error("no compute queue");

            uint32_t nde = 0;
            vkEnumerateDeviceExtensionProperties(phys, nullptr, &nde, nullptr);
            std::vector<VkExtensionProperties> de(nde);
            vkEnumerateDeviceExtensionProperties(phys, nullptr, &nde, de.data());
            std::vector<const char*> dext;
            for (auto& e : de)
                if (!strcmp(e.extensionName, "VK_KHR_portability_subset"))
                    dext.push_back("VK_KHR_portability_subset");
            bool haveExtHost = false, haveKhrExtMem = false;
            for (auto& e : de) {
                haveExtHost |= !strcmp(e.extensionName, "VK_EXT_external_memory_host");
                haveKhrExtMem |= !strcmp(e.extensionName, "VK_KHR_external_memory");
            }
            if (haveExtHost && haveKhrExtMem && !getenv("GPU_POC_NO_HOST_IMPORT")) {
                dext.push_back("VK_KHR_external_memory");
                dext.push_back("VK_EXT_external_memory_host");
                hostImport = true;
            }

            float prio = 1.f;
            VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            qci.queueFamilyIndex = qfam;
            qci.queueCount = 1;
            qci.pQueuePriorities = &prio;
            VkPhysicalDeviceFeatures enabled{};
            enabled.shaderInt64 = feats.shaderInt64;
            enabled.shaderFloat64 = feats.shaderFloat64;
            VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            dci.queueCreateInfoCount = 1;
            dci.pQueueCreateInfos = &qci;
            dci.enabledExtensionCount = (uint32_t)dext.size();
            dci.ppEnabledExtensionNames = dext.data();
            dci.pEnabledFeatures = &enabled;
            VK_CHECK(vkCreateDevice(phys, &dci, nullptr, &dev));
            volkLoadDevice(dev);
            vkGetDeviceQueue(dev, qfam, 0, &queue);

            VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            cpi.queueFamilyIndex = qfam;
            cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            VK_CHECK(vkCreateCommandPool(dev, &cpi, nullptr, &cmdPool));
            VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            cai.commandPool = cmdPool;
            cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cai.commandBufferCount = 1;
            VK_CHECK(vkAllocateCommandBuffers(dev, &cai, &cmd));
            VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096};
            VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            dpi.maxSets = 512;
            dpi.poolSizeCount = 1;
            dpi.pPoolSizes = &ps;
            VK_CHECK(vkCreateDescriptorPool(dev, &dpi, nullptr, &descPool));
            VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            VK_CHECK(vkCreateFence(dev, &fi, nullptr, &fence));

            // Pick host-visible+coherent memory. Unified-memory devices (integrated GPUs, Apple, CPU drivers) use
            // memory that is also device-local. A discrete GPU can expose a small host-visible window of VRAM
            // (the PCIe BAR) that looks "unified" but is not, so it is decided by the device type: the host
            // buffer is then system memory and device-local twins are added below (staging copies).
            VkPhysicalDeviceMemoryProperties mp;
            vkGetPhysicalDeviceMemoryProperties(phys, &mp);
            const bool discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && !getenv("GPU_POC_FORCE_HOST_MEMORY");
            const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            bool ok = false;
            if (!discrete)
                for (uint32_t i = 0; i < mp.memoryTypeCount && !ok; i++)
                    if ((mp.memoryTypes[i].propertyFlags & (want | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) ==
                        (want | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                        memType = i;
                        unifiedMemory = true;
                        ok = true;
                    }
            // Staging buffers are read back by the host, so prefer cached system memory: reading uncached
            // (write-combined) memory is extremely slow and would be counted as GPU time.
            if (discrete)
                for (uint32_t i = 0; i < mp.memoryTypeCount && !ok; i++)
                    if ((mp.memoryTypes[i].propertyFlags & (want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) ==
                        (want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
                        memType = i;
                        ok = true;
                    }
            for (uint32_t i = 0; i < mp.memoryTypeCount && !ok; i++)  // system memory first on a discrete GPU
                if ((mp.memoryTypes[i].propertyFlags & (want | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) == want) {
                    memType = i;
                    ok = true;
                }
            for (uint32_t i = 0; i < mp.memoryTypeCount && !ok; i++)
                if ((mp.memoryTypes[i].propertyFlags & want) == want) {
                    memType = i;
                    ok = true;
                }
            if (!ok)
                throw std::runtime_error("no host-visible coherent memory");
            // Discrete GPU: also find device-local memory that the host cannot map.
            if (!unifiedMemory && !getenv("GPU_POC_FORCE_HOST_MEMORY"))
                for (uint32_t i = 0; i < mp.memoryTypeCount && !haveDevMem; i++)
                    if ((mp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) ==
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                        devMemType = i;
                        haveDevMem = true;
                    }
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }

    Buf alloc(size_t bytes) {
        Buf b;
        b.size = bytes;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = bytes;
        bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(dev, &bi, nullptr, &b.buf));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(dev, b.buf, &mr);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = memType;
        VK_CHECK(vkAllocateMemory(dev, &mai, nullptr, &b.mem));
        VK_CHECK(vkBindBufferMemory(dev, b.buf, b.mem, 0));
        VK_CHECK(vkMapMemory(dev, b.mem, 0, VK_WHOLE_SIZE, 0, &b.ptr));
        if (haveDevMem) {
            VK_CHECK(vkCreateBuffer(dev, &bi, nullptr, &b.dbuf));
            VkMemoryRequirements dr;
            vkGetBufferMemoryRequirements(dev, b.dbuf, &dr);
            VkMemoryAllocateInfo dai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            dai.allocationSize = dr.size;
            dai.memoryTypeIndex = devMemType;
            VK_CHECK(vkAllocateMemory(dev, &dai, nullptr, &b.dmem));
            VK_CHECK(vkBindBufferMemory(dev, b.dbuf, b.dmem, 0));
        }
        return b;
    }

    // Wraps caller-owned host memory (page-aligned address and size) as a storage buffer without copying it.
    // Only meaningful on unified-memory devices; throws if the driver refuses the import.
    Buf importHost(void* p, size_t bytes) {
        if (!hostImport) throw std::runtime_error("VK_EXT_external_memory_host not available");
        Buf b;
        b.size = bytes; b.ptr = p; b.imported = true;
        VkMemoryHostPointerPropertiesEXT hp{VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
        VK_CHECK(vkGetMemoryHostPointerPropertiesEXT(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, p, &hp));
        VkExternalMemoryBufferCreateInfo eb{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
        eb.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.pNext = &eb;
        bi.size = bytes;
        bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(dev, &bi, nullptr, &b.buf));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(dev, b.buf, &mr);
        uint32_t bits = mr.memoryTypeBits & hp.memoryTypeBits;
        if (!bits) throw std::runtime_error("no memory type accepts this host pointer");
        uint32_t type = 0;
        while (!((bits >> type) & 1)) type++;
        VkImportMemoryHostPointerInfoEXT imp{VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT};
        imp.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
        imp.pHostPointer = p;
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.pNext = &imp;
        mai.allocationSize = bytes;
        mai.memoryTypeIndex = type;
        VK_CHECK(vkAllocateMemory(dev, &mai, nullptr, &b.mem));
        VK_CHECK(vkBindBufferMemory(dev, b.buf, b.mem, 0));
        return b;
    }

    // Record host -> device copy of the first `bytes` (0 = all) of a staging buffer. No-op on unified memory.
    void upload(Buf& b, size_t bytes = 0) {
        if (!b.dbuf) return;
        VkBufferCopy r{0, 0, bytes ? bytes : b.size};
        vkCmdCopyBuffer(cmd, b.buf, b.dbuf, 1, &r);
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    }
    // Record device -> host copy of the first `bytes` (0 = all) into the staging buffer. No-op on unified memory.
    void download(Buf& b, size_t bytes = 0) {
        if (!b.dbuf) return;
        VkMemoryBarrier m1{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        m1.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        m1.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &m1, 0, nullptr, 0, nullptr);
        VkBufferCopy r{0, 0, bytes ? bytes : b.size};
        vkCmdCopyBuffer(cmd, b.dbuf, b.buf, 1, &r);
        VkMemoryBarrier m2{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        m2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        m2.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &m2, 0, nullptr, 0, nullptr);
    }

    void release(Buf& b) {
        if (!b.buf)
            return;
        if (!b.imported) vkUnmapMemory(dev, b.mem);
        vkDestroyBuffer(dev, b.buf, nullptr);
        vkFreeMemory(dev, b.mem, nullptr);
        if (b.dbuf) { vkDestroyBuffer(dev, b.dbuf, nullptr); vkFreeMemory(dev, b.dmem, nullptr); }
        b = Buf{};
    }

    static std::vector<uint32_t> readSpv(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
            throw std::runtime_error("cannot open " + path);
        size_t sz = f.tellg();
        std::vector<uint32_t> v(sz / 4);
        f.seekg(0);
        f.read((char*)v.data(), sz);
        return v;
    }

    // Pipeline creation is where Metal compiles the MSL, so it is timed separately.
    Kernel makeKernel(const std::string& spvPath,
                      uint32_t nbufs,
                      uint32_t pushBytes,
                      const std::vector<uint32_t>& spec = {}) {
        double t0 = nowMs();
        Kernel k;
        k.nbufs = nbufs;
        k.pushBytes = pushBytes;
        auto code = readSpv(spvPath);
        VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smi.codeSize = code.size() * 4;
        smi.pCode = code.data();
        VK_CHECK(vkCreateShaderModule(dev, &smi, nullptr, &k.mod));
        std::vector<VkDescriptorSetLayoutBinding> bs(nbufs);
        for (uint32_t i = 0; i < nbufs; i++)
            bs[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dli.bindingCount = nbufs;
        dli.pBindings = bs.data();
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &dli, nullptr, &k.dsl));
        VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushBytes};
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &k.dsl;
        pli.pushConstantRangeCount = pushBytes ? 1 : 0;
        pli.pPushConstantRanges = &pcr;
        VK_CHECK(vkCreatePipelineLayout(dev, &pli, nullptr, &k.pl));
        std::vector<VkSpecializationMapEntry> me(spec.size());
        for (size_t i = 0; i < spec.size(); i++)
            me[i] = {(uint32_t)i, (uint32_t)(i * 4), 4};
        VkSpecializationInfo si{(uint32_t)spec.size(), me.data(), spec.size() * 4, spec.data()};
        VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    nullptr,
                    0,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    k.mod,
                    "main",
                    spec.empty() ? nullptr : &si};
        pi.layout = k.pl;
        VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pi, nullptr, &k.pipe));
        k.createMs = nowMs() - t0;
        return k;
    }

    void release(Kernel& k) {
        vkDestroyPipeline(dev, k.pipe, nullptr);
        vkDestroyPipelineLayout(dev, k.pl, nullptr);
        vkDestroyDescriptorSetLayout(dev, k.dsl, nullptr);
        vkDestroyShaderModule(dev, k.mod, nullptr);
        k = Kernel{};
    }

    // A recorded sequence of dispatches submitted once (barrier between dispatches).
    void begin() {
        VK_CHECK(vkResetDescriptorPool(dev, descPool, 0));
        VK_CHECK(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    }

    void dispatch(const Kernel& k,
                  const std::vector<const Buf*>& bufs,
                  const void* push,
                  uint32_t gx,
                  uint32_t gy = 1,
                  uint32_t gz = 1) {
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &k.dsl;
        VkDescriptorSet ds;
        VK_CHECK(vkAllocateDescriptorSets(dev, &ai, &ds));
        std::vector<VkDescriptorBufferInfo> bi(bufs.size());
        std::vector<VkWriteDescriptorSet> ws(bufs.size());
        for (size_t i = 0; i < bufs.size(); i++) {
            bi[i] = {bufs[i]->bound(), 0, VK_WHOLE_SIZE};
            ws[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                     nullptr,
                     ds,
                     (uint32_t)i,
                     0,
                     1,
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                     nullptr,
                     &bi[i],
                     nullptr};
        }
        vkUpdateDescriptorSets(dev, (uint32_t)ws.size(), ws.data(), 0, nullptr);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pl, 0, 1, &ds, 0, nullptr);
        if (k.pushBytes)
            vkCmdPushConstants(cmd, k.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, k.pushBytes, push);
        vkCmdDispatch(cmd, gx, gy, gz);
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    // Submit and wait. Returns wall time in ms for submit+wait.
    double submitWait(uint64_t timeoutNs = 10'000'000'000ull) {
        VK_CHECK(vkEndCommandBuffer(cmd));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VK_CHECK(vkResetFences(dev, 1, &fence));
        double t0 = nowMs();
        VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));
        VK_CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, timeoutNs));
        return nowMs() - t0;
    }

    void destroy() {
        if (!dev)
            return;
        vkDeviceWaitIdle(dev);
        vkDestroyFence(dev, fence, nullptr);
        vkDestroyDescriptorPool(dev, descPool, nullptr);
        vkDestroyCommandPool(dev, cmdPool, nullptr);
        vkDestroyDevice(dev, nullptr);
        vkDestroyInstance(inst, nullptr);
        dev = nullptr;
    }
};

inline std::vector<std::string> defaultLoaderPaths() {
    std::vector<std::string> v;
    if (const char* e = getenv("GPU_POC_LOADER"))  // lets tests force a bad path
        v.push_back(e);
    else {
        v = {"libvulkan.dylib", "libvulkan.1.dylib", "/usr/local/lib/libvulkan.dylib",
             "libvulkan.so.1", "libvulkan.so", "vulkan-1.dll"};
    }
    return v;
}

}  // namespace gpu
