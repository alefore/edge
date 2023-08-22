#!/usr/bin/python3

import argparse
import os
import re
import subprocess
import sys
import tempfile
from typing import List, Optional

def MaybeCreateFile(path: str, content: Optional[List[str]],
                    dry_run: bool, diff: bool) -> None:
  if not path or not content:
    return

  if diff:
    print(f"File: {path}: {len(content)}:")
    try:
      with tempfile.NamedTemporaryFile(mode='w', delete=False) as tmp_file:
        tmp_file.writelines(content)
        tmp_file_path = tmp_file.name

      result : CompletedProcess = subprocess.run(
          ['diff', '-Naur', path, tmp_file_path], stdout=subprocess.PIPE,
          text=True)
      print(result.stdout)
    finally:
      os.remove(tmp_file_path)
    return

  if dry_run:
    print(f"File: {path}: {len(content)}:")
    sys.stdout.writelines(content)
    return

  # Create directories if they don't exist
  directory = os.path.dirname(path)
  if not os.path.exists(directory):
    os.makedirs(directory)

  # Write content to the file
  with open(path, 'w') as file:
    file.writelines(content)

def main() -> None:
  parser = argparse.ArgumentParser(
      description='Extract code from Markdown file and create corresponding '
                  'files.')
  parser.add_argument(
      '--output_directory',
      type=str,
      default='/',
      help='Create all files inside this directory.')
  parser.add_argument(
      '--dry_run',
      action='store_true',
      help='Only print the files that would be created.')
  parser.add_argument(
      '--diff',
      action='store_true',
      help='Do a diff with the files created.')

  args = parser.parse_args()

  code_content: Optional[List[str]] = None
  prefix_content: List[str] = []
  file_path: str = ""

  for line in sys.stdin:
    line = line.rstrip()
    match = re.match(r"^File `(.*)`:$", line)

    if match:
      file_path = os.path.join(args.output_directory,
          os.path.relpath(os.path.expanduser(match.group(1)), start='/'))
      code_content = []
      continue

    if not file_path:
      continue

    if line.startswith("    "):
      code_content.extend(prefix_content)
      prefix_content = []
      code_content.append(line[4:] + '\n')
      continue

    if line == "":
      if code_content:
        prefix_content.append(line + '\n')
      continue

    MaybeCreateFile(file_path, code_content, args.dry_run, args.diff)
    file_path = None
    code_content = None
    prefix_content = []

  MaybeCreateFile(file_path, code_content, args.dry_run, args.diff)

if __name__ == "__main__":
    main()
