#!/usr/bin/python

# TODO: Make this less brittle.  If the second patch fails to apply, somehow
# leave files in the original state.

import os
import sys

def Run(cmd):
  print "Running: %s" % cmd
  status = os.system(cmd)
  if status:
    sys.exit(status)

if len(sys.argv) != 3:
  print "Usage: %s PATH_OLD_DIFF PATH_NEW_DIFF" % (sys.argv[0],)
  sys.exit(1)

patch_parameters = os.getenv("PATCH_PARAMETERS") or "-p1"

Run("patch --reverse %s <%s" % (patch_parameters, sys.argv[1]))
Run("patch %s <%s" % (patch_parameters, sys.argv[2]))

print "Done."
