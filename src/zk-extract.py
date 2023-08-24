#!/usr/bin/python3

import argparse
import os
import re
import subprocess
import sys
import tempfile
from typing import Dict, List, Optional, Tuple


class FilesWriter:
  def __init__(self, dry_run: bool, diff: bool):
    self.dry_run: bool = dry_run
    self.diff: bool = diff
    # Key is the input path, value is the actual path used (which most of the
    # time will be the same).
    self.files_written: Dict[str, str] = {}
    self.files_to_delete: List[str] = []

  def __enter__(self):
    return self

  def __exit__(self, type, value, traceback) -> None:
    for input_path, actual_path in self.files_written.items():
      print(f"{input_path}")
      if self.diff:
        result : CompletedProcess = subprocess.run(
            ['diff', '-Naur', input_path, actual_path], stdout=subprocess.PIPE,
            text=True)
        print(result.stdout)

    for path in self.files_to_delete:
      os.remove(path)

  def ReceiveFile(self, path: str, content: Optional[List[str]]) -> None:
    if not path or not content:
      return

    if self.diff and path not in self.files_written:
      with tempfile.NamedTemporaryFile(mode='w', delete=False) as tmp_file:
        tmp_file.writelines(content)
        self.files_written[path] = tmp_file.name
        self.files_to_delete.append(tmp_file.name)
      return

    mode: str = 'a'
    if path not in self.files_written:
      print(f'inserted: {path}')
      self.files_written[path] = path
      mode = 'w'

    if self.dry_run:
      return

    # Create directories if they don't exist
    directory = os.path.dirname(self.files_written[path])
    if not os.path.exists(directory):
      os.makedirs(directory)

    print(f"Mode: {mode}")
    # Write content to the file
    with open(self.files_written[path], mode) as file:
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

  code_content: List[str] = []
  prefix_content: List[str] = []
  file_path: str = ""

  with FilesWriter(args.dry_run, args.diff) as writer:
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

      writer.ReceiveFile(file_path, code_content)
      file_path = ''
      code_content = []
      prefix_content = []

    writer.ReceiveFile(file_path, code_content)

if __name__ == "__main__":
    main()
