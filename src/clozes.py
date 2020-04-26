#!/usr/bin/python3

# Script to parse a Zettelkasten and generate flash cards with cloze deletions
# for Mnemosyne.
#
# It reads all `???.md` files looking for lines like this:
#
#     zkcloze answer hint
#
# `answer` here must be some text in the file.
#
# For each file with such lines, it generates a card with cloze deletions. For
# each such line, it generates a cloze deletion (i.e., a single file can contain
# multiple `zkcloze` lines).
#
# The output is meant to be stored in a Mnemosyne `cards.xml` file, which can
# then be included in a `*.cards` zipfile (along with a `METADATA` file) and
# imported into Mnemosyne.
#
# This script aims to generate the flashcard in a "stable" manner. The IDs in
# the facts are based on the path (where the `zkcloze` lines were found). The
# IDs in the cards are based on the path as well as on the hash of the answer.
# That enables you to make any changes to the file (including adding other
# `zkcloze` lines) without losing the history of existing cards when you
# reimport them.

import glob
import hashlib
import html
import os
import re
import shlex

ID_PREFIX = 'zk-'
TAG_ID = ID_PREFIX + 'tag-00'

def ParseClozeLine(line):
  command, answer, hint = shlex.split(line)
  return answer, hint

facts = []

class FactData(object):
  def __init__(self, path, lines, clozes):
    lines = lines[1:]  # Drop the title.
    index = lines.index("Related:\n")
    if index != -1:
      lines = lines[:index]
    contents = ' '.join(l.strip() for l in lines)
    contents = re.sub(r'\[([^]]*)\]....\.md.', r'\1', contents)
    for answer, hint in clozes:
      contents = contents.replace(answer, "[" + answer + ":" + hint + "]")
    self.path = path
    self.path_base = os.path.splitext(path)[0]
    self.contents = contents
    self.clozes = clozes

  def Id(self):
    return ID_PREFIX + 'fact-' + self.path_base

  def Print(self):
    print('<log type="16" o_id="%s"><text>%s</text></log>' % (
        self.Id(), html.escape(self.contents)))

  def PrintClozes(self):
    for i in range(len(self.clozes)):
      answer, hint = self.clozes[i]
      cloze_id = (
          ID_PREFIX + 'card-' + self.path_base + '-'
          + hashlib.sha224(answer.encode('utf-8')).hexdigest())
      print('<log type="6" o_id="%s" card_t="5" fact="%s" '
            'fact_v="5.1" tags="%s" gr="-1" e="2.5" ac_rp="0" rt_rp="0" '
            'lps="0" ac_rp_l="0" rt_rp_l="0" l_rp="-1" n_rp="-1">' % (
                cloze_id, self.Id(), TAG_ID))
      print("<extra>{'cloze': '%s:%s', 'index': %s}</extra></log>" % (
                html.escape(answer), html.escape(hint), i))

for path in glob.glob('???.md'):
  with open(path) as f:
    lines = list(f)
  clozes = [];
  for line in lines:
    if line.startswith("zkcloze "):
      clozes.append(ParseClozeLine(line))
  if clozes:
    facts.append(FactData(path, lines, clozes))

tags = [
    '<log type="10" o_id="%s"><name>Zettelkasten</name></log>' % TAG_ID]

print('<openSM2sync number_of_entries="%s">' % (
          len(tags) + len(facts) + sum(len(c.clozes) for c in facts)))

for t in tags:
  print(t)

# Print the fact items:
for card in facts:
  card.Print()
  card.PrintClozes()

print('</openSM2sync>')
