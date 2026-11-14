#!/bin/bash
# Optional, local-development only: a first-come-first-served turn queue for the ONE shared GitHub-hosted T4 runner (label gpu-t4,
# pool size 1) that the mongo and pg-strom repos in zrecore-org both use. It only coordinates pushes made from this machine; GitHub's
# own queue still decides who gets the runner. Same idea as with_gpu_lock.sh, but for CI. Nothing in CI uses it.
#
#   t4_ci_turn.sh join <who> [note]   add <who> (e.g. mongo, pg-strom) to the queue, once
#   t4_ci_turn.sh wait <who>          block until <who> is first in line AND no T4 job is queued or running in either repo
#   t4_ci_turn.sh leave <who>         remove <who> (call it when your T4 job has finished, failed, or you give up)
#   t4_ci_turn.sh status              show the queue and any busy T4 jobs
#
# Rules: one entry per who; strict first-in first-out; a fix push after a failed run joins at the back; entries older than 4 hours are
# ignored (a crashed session must not block the runner forever). QUEUE can be overridden for testing. Needs `gh`.
QUEUE=${T4_CI_QUEUE:-/tmp/gpu_t4_ci.queue}
REPOS=${T4_CI_REPOS:-"zrecore-org/mongo zrecore-org/pg-strom"}
MAXAGE=${T4_CI_MAXAGE:-14400}
touch "$QUEUE"
live() { local now; now=$(date +%s); awk -v n="$now" -v m="$MAXAGE" '($1 + m) > n' "$QUEUE"; }
busy() {   # run ids whose T4 job is queued or in progress
  for r in $REPOS; do
    for id in $(gh run list -R "$r" --limit 6 --json databaseId,status -q '.[]|select(.status!="completed")|.databaseId' 2>/dev/null); do
      gh run view -R "$r" "$id" --json jobs -q ".jobs[]|select((.name|test(\"T4\")) and (.status!=\"completed\"))|\"$r#$id\"" 2>/dev/null
    done
  done | sort -u
}
case "$1" in
  join)  grep -q " $2 " "$QUEUE" && grep -q "^[0-9]* $2 " "$QUEUE" || echo "$(date +%s) $2 ${3:-}" >> "$QUEUE"; live | head -5 ;;
  leave) grep -v "^[0-9]* $2 " "$QUEUE" > "$QUEUE.tmp"; mv "$QUEUE.tmp" "$QUEUE"; echo "$2 left the queue" ;;
  status) echo "queue:"; live | awk '{print "  " $0}'; echo "busy T4 jobs: $(busy | tr '\n' ' ')" ;;
  wait)
    while true; do
      first=$(live | head -1 | awk '{print $2}')
      if [ "$first" = "$2" ]; then
        b=$(busy | tr '\n' ' ')
        if [ -z "$b" ]; then sleep 45; b=$(busy | tr '\n' ' '); [ -z "$b" ] && break; fi   # re-check, a run may just be starting
        echo "$(date -u +%H:%M:%S) first in line, T4 busy: $b"
      else
        echo "$(date -u +%H:%M:%S) waiting: first in line is ${first:-nobody (you are not in the queue)}"
      fi
      sleep 90
    done
    echo "$(date -u +%H:%M:%S) $2 is first in line and the T4 is free" ;;
  *) sed -n 2,14p "$0"; exit 2 ;;
esac
