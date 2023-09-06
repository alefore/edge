#!/usr/bin/python3
#
# python3 ~/edge-clang/edge/src/python/mappings.py --dictionary <( (cat /usr/share/dict/american-english; cat /usr/share/dict/ngerman) | egrep -iv '^(zk|b|c|d|e|f|g|h|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z)$' ) --model ~/edge-clang/edge/mappings/model.txt ~/zettelkasten/???.md

import argparse
import heapq
import re
import sys
from collections import defaultdict
from typing import Dict, List, NewType, Optional, Set

CompressedText = NewType('CompressedText', str)
Text = NewType('Text', str)


def Difficulty(word: Text) -> float:
  left: str = "qwert" + "asdfg" + "zxcvb" + "äáé"
  right: str = "yuiophjkl;nm"
  special: str = "áéíóúäëüïößñ"
  top: str = "qwertyuiopéúíó"
  bottom: str = "zxcvbnmä"
  saving: float = 0
  current_side: Optional[str] = None
  last_character: Optional[str] = None
  for c in word:
    if c in special:
      saving += 1.5
    saving += 0.8
    next_side = 'left' if c in left else 'right'
    if c == last_character:
      saving -= 0.1
    elif current_side is not None and current_side == next_side:
      saving += 0.4
    current_side = next_side
    if c in bottom:
      saving += 0.3
    elif c in top:
      saving += 0.1
  return saving


class TextMapping:

  def __init__(self, penalty: float = 0.2):
    self.dictionary: Set[Text] = set()
    self.penalty: float = penalty
    self.freq_dict: Dict[Text, int] = defaultdict(int)
    self.mappings: Dict[CompressedText, List[Text]] = {}
    self.model: Dict[CompressedText, Text] = {}

  def LoadDictionaryFile(self, dict_file: str) -> None:
    with open(dict_file, 'r') as f:
      self.dictionary.update(Text(word.strip().lower()) for word in f)

  def LoadInputFile(self, file: str) -> None:
    with open(file, 'r') as f:
      text: str = f.read().lower()
    sentences = re.split(r'(?<=[.!?:])\s+|\n\n', text)
    for sentence in sentences:
      chain: List[Text] = []
      for word in re.findall(r'\b\w+\b', sentence):
        if Text(word) in self.dictionary:
          chain = chain[-2:] + [Text(word)]
          for i in range(0, len(chain)):
            self.freq_dict[Text(' '.join(chain[i:]))] += 1
        else:
          chain = []

  def LoadModel(self, file: str) -> None:
    with open(file, 'r') as f:
      for l in f:
        if l:
          key, value = l.strip().lower().split(maxsplit=1)
          self.model[CompressedText(key)] = Text(value)

  def CompressionIndex(self, alias: CompressedText, text: Text) -> float:
    if alias == text:
      return 0
    return self.freq_dict[text] * (Difficulty(text) - len(alias) - self.penalty)

  def _IsViableMapping(self, alias: CompressedText, word: Text):
    if alias in self.model or word in self.reverse_model:
      return False
    if not word.startswith(alias[0]) or self.freq_dict[word] <= 1:
      return False
    word_index = 1
    for i in range(1, len(alias)):
      word_index = word.find(alias[i], word_index)
      if word_index == -1:
        return False
      word_index += 1
    return True

  def _FindMapping(self, prefix: CompressedText):
    if prefix in self.model:
      return
    possible_words = [
        word for word in self.freq_dict.keys()
        if self._IsViableMapping(prefix, word)
    ]
    if possible_words:
      self.mappings[prefix] = heapq.nlargest(
          4,
          possible_words,
          key=lambda word: self.CompressionIndex(prefix, word))
    else:
      print(f"No words found for: {prefix}", file=sys.stderr)

  def Compute(self) -> None:
    self.reverse_model = {t: s for s, t in self.model.items()}
    for i in range(97, 123):  # a to z
      alias = chr(i)
      self._FindMapping(CompressedText(alias))

      for j in range(97, 123):  # a to z
        self._FindMapping(CompressedText(chr(i) + chr(j)))

  def _GetReverseMappings(self) -> Dict[Text, List[CompressedText]]:
    output: Dict[Text, List[CompressedText]] = defaultdict(list)
    prefix: CompressedText
    words: List[Text]
    for prefix, words in self.mappings.items():
      for word in words:
        output[word].append(prefix)
    return output

  def AugmentModel(self):
    prefix: CompressedText
    words: List[Text]
    for prefix, words in self.mappings.items():
      assert prefix not in self.model
      assert words[0] not in self.reverse_model
      self.model[prefix] = words[0]

  def Display(self):
    reverse_dict: Dict[Text, List[CompressedText]] = self._GetReverseMappings()
    self.AugmentModel()

    print(f"Words in corpus: {len(self.freq_dict)}", file=sys.stderr)

    for word, prefixes in reverse_dict.items():
      print(
          f"{word} (freq: {self.freq_dict[word]}) {prefixes}", file=sys.stderr)

    self.ShowModel()
    self.ShowTotalCompressionIndex()

  def _GetWordData(self, prefix: CompressedText, word: Text) -> str:
    index: float = self.CompressionIndex(prefix, word)
    freq: int = self.freq_dict[word]
    return f"{index:.1f} (times: {freq})"

  def ShowModel(self):
    prefix: CompressedText
    for prefix in sorted(
        self.model, key=lambda p: self.CompressionIndex(p, self.model[p])):
      word: Text = self.model[prefix]
      compression = round(self.CompressionIndex(prefix, word))
      other_options: str = ""
      if prefix in self.mappings:
        other_options = ''.join(f", {f} {self._GetWordData(prefix, f)}"
                                for f in self.mappings[prefix]
                                if f != word)
      print(
          f"{prefix} {word} # {self._GetWordData(prefix, word)}{other_options}")

  def ShowTotalCompressionIndex(self):
    best_model: Dict[Text, CompressedText] = {}
    for prefix, word in self.model.items():
      new_value: float = self.CompressionIndex(prefix, word)
      if word in best_model and self.CompressionIndex(best_model[word],
                                                      word) > new_value:
        continue
      best_model[word] = prefix
    assert len(set(best_model.values())) == len(best_model)
    index: float = 0
    index = sum(self.CompressionIndex(p, w) for w, p in best_model.items())
    print(f"Total compression: {index}", file=sys.stderr)


if __name__ == "__main__":
  parser = argparse.ArgumentParser(
      description="Generate mappings based on a corpus and dictionary.")
  parser.add_argument(
      '--model', help='Path to a model file ("source target" lines).')
  parser.add_argument(
      '--dictionary',
      required=True,
      action='append',
      help='Path to a dictionary file.')
  parser.add_argument(
      'files', nargs='+', help='List of markdown files to process.')
  args = parser.parse_args()

  mapper = TextMapping()
  for file in args.dictionary:
    mapper.LoadDictionaryFile(file)
  if args.model:
    mapper.LoadModel(args.model)
  for file in args.files:
    mapper.LoadInputFile(file)
  mapper.Compute()
  mapper.Display()
