#!/usr/bin/python3

import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile
from typing import Dict, List, Optional, Set, TextIO, Tuple


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

    self.collecting_dependencies: bool = False
    self.debian_packages: Set[str] = set()
    self.path: str = ''
    self.content: List[str] = []
    self.prefix_empty_lines: int = 0

  def __enter__(self):
    return self

  def __exit__(self, type, value, traceback) -> None:
    self.FlushFile()
    for input_path, actual_path in self.files_written.items():
      if self.diff:
        result: subprocess.CompletedProcess = subprocess.run(
            [
                'diff', '--label',
                os.path.join('/old/', os.path.relpath(input_path,
                                                      start='/')), '--label',
                os.path.join('/new/', os.path.relpath(input_path, start='/')),
                '-Naur', input_path, actual_path
            ],
            stdout=subprocess.PIPE,
            text=True)
        if (result.stdout.strip()):
          print(result.stdout)
      else:
        mode_string = ''
        if input_path in self.file_mode:
          mode_string = ' ' + oct(self.file_mode[input_path])[2:]
        print(f"{input_path}{mode_string}")

    for package_name in self.debian_packages:
      self._HandleDebianPackage(package_name)

    if not self.dry_run:
      for path in self.files_to_delete:
        os.remove(path)
      if not self.diff:
        for path in self.file_mode:
          os.chmod(path, self.file_mode[path])

    if self.diff:
      for path, expected_mode in self.file_mode.items():
        if path in self.files_with_errors:
          continue
        try:
          found_mode = int(oct(os.stat(path).st_mode)[-3:], 8)
          if found_mode != expected_mode:
            print(f"{path}: Expected {oct(expected_mode)[2:]}: "
                  f"Found {oct(found_mode)[2:]}")
        except PermissionError as exception:
          self.files_with_errors[path] = exception
        except FileNotFoundError as exception:
          self.files_with_errors[path] = exception

    if self.files_with_errors:
      for file, exception_found in self.files_with_errors.items():
        print(f"{file}: {exception_found}", file=sys.stderr)
      sys.exit(2)

  def FlushFile(self) -> None:
    if self.IsCollectingFile():
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

  def AddDebianPackage(self, package_name: str) -> None:
    assert not self.IsCollectingFile()
    assert self.IsCollectingDependencies()
    self.debian_packages.add(package_name)

  def _HandleDebianPackage(self, package_name):
    if self.dry_run:
      print(f'Debian package: {package_name}')
      return

    if not self.diff:
      subprocess.call(['su', '-c', f"apt-get install -y {package_name}"])
      return

    result: subprocess.CompletedProcess = subprocess.run(
        ['dpkg', '-s', package_name], capture_output=True, text=True)
    if 'Status: install ok installed' not in result.stdout:
      print(f"Debian package missing: {package_name}")

  def StartCollectingDependencies(self) -> None:
    self.FlushFile()
    self.collecting_dependencies = True

  def StartCollectingFile(self, path: str, mode: Optional[int]) -> None:
    self.FlushFile()
    self.collecting_dependencies = False
    self.path = os.path.join(
        self.output_directory,
        os.path.relpath(os.path.expanduser(path), start='/'))
    if mode:
      self.file_mode[self.path] = mode

  def IsCollectingFile(self) -> bool:
    return self.path != ''

  def IsCollectingDependencies(self) -> bool:
    return self.collecting_dependencies

  def AddCode(self, line) -> None:
    if self.IsCollectingFile():
      self.content.extend('\n' * self.prefix_empty_lines)
      self.prefix_empty_lines = 0
      self.content.append(line + '\n')

  def AddPrefixEmptyLine(self):
    if self.IsCollectingFile() and self.content:
      self.prefix_empty_lines += 1


def ProcessFile(writer: FilesWriter, input: TextIO) -> None:
  for line in input:
    line = line.rstrip()

    match_file = re.match(r"^File `(.*)` *(\(mode ([0-9]{3})\))?:$", line)
    match_dependencies = re.match(r"^Dependencies:$", line)
    match_debian_package = re.match(r"\* Debian package: `([^`]+)`$", line)
    if match_file:
      mode: Optional[int] = None
      if match_file.group(3):
        mode = int(match_file.group(3), 8)
      writer.StartCollectingFile(match_file.group(1), mode)

    elif match_dependencies:
      writer.FlushFile()
      writer.StartCollectingDependencies()

    elif line.startswith("    ") and writer.IsCollectingFile():
      writer.AddCode(line[4:])

    elif line == "":
      writer.AddPrefixEmptyLine()

    elif match_debian_package:
      writer.AddDebianPackage(match_debian_package.group(1))

    else:
      writer.FlushFile()

  writer.FlushFile()


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
