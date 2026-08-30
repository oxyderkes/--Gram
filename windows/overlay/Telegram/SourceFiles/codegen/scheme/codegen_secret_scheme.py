'''
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''
import os
import sys

sys.dont_write_bytecode = True
scriptPath = os.path.dirname(os.path.realpath(__file__))
sys.path.append(scriptPath + '/../../../lib_tl/tl')
from generate_tl import generate

generate({
  'namespaces': {
    'global': 'Secret',
    'creator': 'details',
  },
  'prefixes': {
    'type': 'MTP',
    'data': 'MTPD',
    'id': 'mtpc',
    'construct': 'MTP_',
  },
  'types': {
    'prime': 'mtpPrime',
    'typeId': 'mtpTypeId',
    'buffer': 'mtpBuffer',
  },
  'sections': [
    'read-write',
  ],
  'typeIdExceptions': [
    'decryptedMessage8#1f814f1f',
    'decryptedMessageService8#aa48327d',
    'decryptedMessageMediaPhoto8#32798a8c',
    'decryptedMessageMediaVideo8#4cee6ef3',
    'decryptedMessageMediaDocument8#b095434b',
    'decryptedMessageMediaAudio8#6080758f',
    'decryptedMessage23#204d3878',
    'decryptedMessageMediaVideo23#524a415d',
    'documentAttributeSticker23#fb0a5727',
    'documentAttributeVideo23#5910cccb',
    'documentAttributeAudio23#51448e5',
    'documentAttributeAudio45#ded218e0',
    'decryptedMessage46#36b091de',
    'decryptedMessageMediaDocument46#7afe8ae2',
    'decryptedMessageMediaDocument#6abd9782',
  ],
  'skip': [
    'int ? = Int;',
    'long ? = Long;',
    'double ? = Double;',
    'string ? = String;',
    'bytes = Bytes;',
    'vector {t:Type} # [ t ] = Vector t;',
  ],
  'builtin': [
    'int',
    'long',
    'double',
    'string',
    'bytes',
  ],
  'builtinTemplates': [
    'vector',
    'flags',
  ],
  'synonyms': {
    'bytes': 'string',
  },
  'builtinInclude': 'mtproto/core_types.h',
  'optimizeSingleData': True,
})
