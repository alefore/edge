#!/usr/bin/python3

import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile
from typing import Dict, List, Optional, TextIO, Tuple


class FilesWriter:
  def __init__(self, output_directory: str, dry_run: bool, diff: bool):
    self.output_directory = output_directory
    self.dry_run: bool = dry_run
    self.diff: bool = diff
    # Key is the input path, value is the actual path used (which most of the
    # time will be the same).
    self.files_written: Dict[str, str] = {}
    self.files_to_delete: List[str] = []
    self.files_with_errors: Dict[str, Exception] = {}
    self.file_mode: Dict[str, int] = {}

    self.path: str = ''
    self.content: List[str] = []
    self.prefix_empty_lines: int = 0

  def __enter__(self):
    return self

  def __exit__(self, type, value, traceback) -> None:
    self.Flush()
    for input_path, actual_path in self.files_written.items():
      if self.diff:
        result : subprocess.CompletedProcess = subprocess.run(
            ['diff',
             '--label',
             os.path.join('/old/', os.path.relpath(input_path, start='/')),
             '--label',
             os.path.join('/new/', os.path.relpath(input_path, start='/')),
             '-Naur', input_path,
             actual_path], stdout=subprocess.PIPE,
            text=True)
        if (result.stdout.strip()):
          print(result.stdout)
      else:
        mode_string = ''
        if input_path in self.file_mode:
          mode_string = ' ' + oct(self.file_mode[input_path])[2:]
        print(f"{input_path}{mode_string}")

    if not self.dry_run:
      for path in self.files_to_delete:
        os.remove(path)
      for path in self.file_mode:
        os.chmod(path, self.file_mode[path])

    if self.files_with_errors:
      for file, exception in self.files_with_errors.items():
        print(f"{file}: {exception}", file=sys.stderr)
      sys.exit(2)

  def Flush(self) -> None:
    if self.IsCollecting():
      try:
        if self.path in self.files_with_errors:
          return

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

      except PermissionError as exception:
        self.files_with_errors[self.path] = exception
      finally:
        self.path = ''
        self.content = []
        self.prefix_empty_lines = 0

  def StartCollecting(self, path: str, mode: Optional[int]) -> None:
    self.Flush()
    self.path = os.path.join(self.output_directory,
        os.path.relpath(os.path.expanduser(path), start='/'))
    if mode:
      self.file_mode[self.path] = mode

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

    match = re.match(
        r"^File `(.*)` *(\(mode ([0-9]{3})\))?:$", line)
    if match:
      mode: Optional[int] = None
      if match.group(3):
        mode = int(match.group(3), 8)
      writer.StartCollecting(match.group(1), mode)

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
