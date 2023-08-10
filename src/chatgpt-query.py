# The first argument should be the path to the conversation log. It starts
# empty and is updated by each invocation to this script. The script reads a
# message from stdin, sends it to ChatGPT and prints the response.
#
# Example:
#
#   ~/local/bin/python3 src/chatgpt-query.py /tmp/log-chatgpt-query.py < file

from typing import Dict, List, Literal, TypedDict, TypeAlias, Union
import openai
import sys
import json
import os
import logging

openai_dir: str = os.path.join(os.path.expanduser('~'), '.openai')
system_prompt_path: str = os.path.join(openai_dir, 'prompt.txt')

CONVERSATION_KEY: str = 'conversation'

DEFAULT_AI_PROMPT: str = 'You are a helpful assistant.'

logging.basicConfig(level=logging.WARNING)

with open(os.path.join(openai_dir, 'api_key'), 'r') as f:
  openai.api_key = f.read().strip()

class ChatEntry(TypedDict):
  role: Literal['system', 'user', 'assistant']
  content: str

def RoleContent(role: Literal['system', 'user', 'assistant'], content: str) -> ChatEntry:
  return {'role': role, 'content': content}

def Chat(log: List[ChatEntry]) -> str:
  """Send a conversation to ChatGPT and return its first response."""
  response = openai.ChatCompletion.create(model='gpt-3.5-turbo', messages=log)
  return response['choices'][0]['message']['content']

def LoadOrCreateLog(log_path: str) -> List[ChatEntry]:
  try:
    with open(log_path, 'r') as file:
      return json.load(file)[CONVERSATION_KEY]
  except FileNotFoundError:
    logging.warning(f"File {log_path} not found. Creating conversation log.")
  return [RoleContent('system', GetInitialPrompt())]

def GetInitialPrompt() -> str:
  try:
    with open(system_prompt_path, 'r') as file:
      return file.read()
  except FileNotFoundError:
    logging.warning(f"File {system_prompt_path} not found. "
                    f"Using default prompt.")
    return DEFAULT_AI_PROMPT

def main() -> None:
  if len(sys.argv) != 2:
    logging.error(f"Usage: {sys.argv[0]} <log_path>")
    sys.exit(1)
  log_path: str = sys.argv[1]
  log = LoadOrCreateLog(log_path)
  log.append(RoleContent('user', sys.stdin.read()))
  response = Chat(log)
  print(response)
  log.append(RoleContent('assistant', response))
  with open(log_path, 'w') as file:
    json.dump({CONVERSATION_KEY: log}, file, indent=True)

if __name__ == '__main__':
    main()
