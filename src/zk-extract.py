#!/usr/bin/python3

import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile
from typing import Dict, List, TextIO, Tuple


class FilesWriter:
  def __init__(self, output_directory: str, dry_run: bool, diff: bool):
    self.output_directory = output_directory
    self.dry_run: bool = dry_run
    self.diff: bool = diff
    # Key is the input path, value is the actual path used (which most of the
    # time will be the same).
    self.files_written: Dict[str, str] = {}
    self.files_to_delete: List[str] = []

    self.path: str = ''
    self.content: List[str] = []
    self.prefix_empty_lines: int = 0

  def __enter__(self):
    return self

  def __exit__(self, type, value, traceback) -> None:
    self.Flush()
    for input_path, actual_path in self.files_written.items():
      print(f"{input_path}")
      if self.diff:
        result : subprocess.CompletedProcess = subprocess.run(
            ['diff', '-Naur', input_path, actual_path], stdout=subprocess.PIPE,
            text=True)
        print(result.stdout)

    for path in self.files_to_delete:
      os.remove(path)

  def Flush(self) -> None:
    if self.IsCollecting():
      try:
        if self.diff and self.path not in self.files_written:
          with tempfile.NamedTemporaryFile(mode='w', delete=False) as tmp_file:
            tmp_file.writelines(self.content)
            self.files_written[self.path] = tmp_file.name
            self.files_to_delete.append(tmp_file.name)
          return

        mode: str = 'a'
        if self.path not in self.files_written:
          self.files_written[self.path] = self.path
          mode = 'w'

        if self.dry_run:
          return

        # Create directories if they don't exist
        directory = os.path.dirname(self.files_written[self.path])
        if not os.path.exists(directory):
          os.makedirs(directory)

        # Write content to the file
        with open(self.files_written[self.path], mode) as file:
          file.writelines(self.content)

      finally:
        self.path = ''
        self.content = []
        self.prefix_empty_lines = 0

  def StartCollecting(self, path) -> None:
    self.Flush()
    self.path = os.path.join(self.output_directory,
        os.path.relpath(os.path.expanduser(path), start='/'))

  def IsCollecting(self) -> bool:
    return self.path != ''

  def AddCode(self, line) -> None:
    if self.IsCollecting():
      self.content.extend('\n' * self.prefix_empty_lines)
      self.prefix_empty_lines = 0
      self.content.append(line + '\n')

  def AddPrefixEmptyLine(self):
    if self.IsCollecting() and self.content:
      self.prefix_empty_lines += 1


def ProcessFile(writer: FilesWriter, input: TextIO) -> None:
  for line in input:
    line = line.rstrip()

    match = re.match(r"^File `(.*)`:$", line)
    if match:
      writer.StartCollecting(match.group(1))

    elif line.startswith("    "):
      writer.AddCode(line[4:])

    elif line == "":
      writer.AddPrefixEmptyLine()

    else:
      writer.Flush()

  writer.Flush()

def main() -> None:
  parser = argparse.ArgumentParser(
      description='Extract code from Markdown file and create corresponding '
                  'files')
  parser.add_argument(
      '--output_directory',
      type=str,
      default='/',
      help='Create all files inside this directory')
  parser.add_argument(
      '--dry_run',
      action='store_true',
      help='Only list the files that would be created')
  parser.add_argument(
      '--diff',
      action='store_true',
      help='Show diff with all the files (without creating them)')
  parser.add_argument(
      'files',
      metavar='PATH',
      type=str,
      nargs='*',
      help='Path to read (if empty, stdin)')
  args = parser.parse_args()

  if len(args.files) != len(set(args.files)):
    file_counts = collections.Counter(args.files)
    repeated_files = [f for f in args.files if file_counts[f] > 1]
    print("Err: Repeated inputs: " + " ".join(repeated_files), file=sys.stderr)
    sys.exit(1)

  with FilesWriter(args.output_directory, args.dry_run, args.diff) as writer:
    if args.files:
      for path in args.files:
        with open(path, 'r') as input_file:
          ProcessFile(writer, input_file)
    else:
      ProcessFile(writer, sys.stdin)

if __name__ == "__main__":
    main()
