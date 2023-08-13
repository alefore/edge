# The first argument should be the path to the conversation log. It starts
# empty and is updated by each invocation to this script. The script reads a
# message from stdin, sends it to ChatGPT and prints the response.
#
# Example:
#
#   ~/local/bin/python3 src/chatgpt-query.py /tmp/log-chatgpt-query.py < file

from typing import Dict, List, Literal, TypedDict, TypeAlias, Union
import argparse
import openai
import sys
import json
import os
import logging

CONVERSATION_KEY: str = 'conversation'

logging.basicConfig(level=logging.WARNING)

def SetApiKey(path: str):
  try:
    with open(path, 'r') as f:
      openai.api_key = f.read().strip()
  except FileNotFoundError:
    logging.error(f"{path}: Unable to load API key.")

class ChatEntry(TypedDict):
  role: Literal['system', 'user', 'assistant']
  content: str

def CreateChatEntry(
    role: Literal['system', 'user', 'assistant'], content: str) -> ChatEntry:
  return {'role': role, 'content': content}

def Chat(log: List[ChatEntry]) -> str:
  """Send a conversation to ChatGPT and return its first response."""
  response = openai.ChatCompletion.create(model='gpt-3.5-turbo', messages=log)
  return response['choices'][0]['message']['content']

def LoadConversation(path: str) -> List[ChatEntry]:
  with open(path, 'r') as file:
    return json.load(file)[CONVERSATION_KEY]

def GetInitialPrompt(prompt_path: str, default_prompt: str) -> str:
  try:
    if prompt_path:
      with open(prompt_path, 'r') as file:
        return file.read()
  except FileNotFoundError:
    pass

  logging.warning(f"File {prompt_path} not found. Using default prompt.")
  return default_prompt

def main() -> None:
  parser = argparse.ArgumentParser(
      description='Hold a conversation with ChatGPT.')
  parser.add_argument(
      "--api_key",
      type=str,
      help="Path to file containing the API key.",
      default=os.path.join(os.path.expanduser('~'), '.openai', 'api_key'))
  parser.add_argument(
      "--prompt",
      type=str,
      help="Path of file with the initial prompt.")
  parser.add_argument(
      "--default_prompt",
      type=str,
      default='You are a helpful assistant.',
      help="Initial text to use as the system prompt.")
  parser.add_argument(
      "--conversation",
      type=str,
      help="Path to the conversation log file.")

  args = parser.parse_args()

  SetApiKey(args.api_key)

  log: List[ChatEntry] = None
  if args.conversation:
    try:
      log = LoadConversation(args.conversation)
    except FileNotFoundError:
      logging.info(f"File {args.conversation} not found. Creating it.")
  if log is None:
    log = [CreateChatEntry(
               'system', GetInitialPrompt(args.prompt, args.default_prompt))]

  log.append(CreateChatEntry('user', sys.stdin.read()))
  response = Chat(log)
  print(response)
  if args.conversation:
    log.append(CreateChatEntry('assistant', response))
    with open(args.conversation, 'w') as file:
      json.dump({CONVERSATION_KEY: log}, file, indent=True)

if __name__ == '__main__':
    main()
