#!/usr/bin/python

# TODO: Make this less brittle.  If the second patch fails to apply, somehow
# leave files in the original state.

import os
import sys

def Run(cmd):
  status = os.system(cmd)
  if status:
    print "Error in command: %s" % cmd
  return status

def RunOrDie(cmd):
  status = Run(cmd)
  if status:
    sys.exit(status)

if len(sys.argv) != 3:
  print "Usage: %s PATH_OLD_DIFF PATH_NEW_DIFF" % (sys.argv[0],)
  sys.exit(1)

patch_parameters = os.getenv("PATCH_PARAMETERS") or "-p1"

RunOrDie("patch --reverse %s <%s" % (patch_parameters, sys.argv[1]))

dry_run_status = Run("patch --dry-run %s <%s" % (patch_parameters, sys.argv[2]))
if dry_run_status:
  print "Reverting to old state."
  RunOrDie("patch %s <%s" % (patch_parameters, sys.argv[1]))
  sys.exit(dry_run_status)

RunOrDie("patch %s <%s" % (patch_parameters, sys.argv[2]))

print "Done."
