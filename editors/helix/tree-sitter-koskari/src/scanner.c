/*
 * External scanner for tree-sitter-koskari.
 * Emits indent and dedent tokens for layout block parsing.
 */

#include "tree_sitter/parser.h"
#include <stdlib.h>
#include <string.h>

#define CAPACITY 32

enum TokenType {
  INDENT,
  DEDENT,
};

typedef struct {
  uint16_t *indent_stack;
  uint32_t size;
  uint32_t capacity;
} Scanner;

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }
static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static void push_indent(Scanner *scanner, uint16_t indent) {
  if (scanner->size >= scanner->capacity) {
    scanner->capacity *= 2;
    scanner->indent_stack = realloc(scanner->indent_stack,
                                    scanner->capacity * sizeof(uint16_t));
  }
  scanner->indent_stack[scanner->size++] = indent;
}

static uint16_t pop_indent(Scanner *scanner) {
  if (scanner->size == 0)
    return 0;
  return scanner->indent_stack[--scanner->size];
}

static uint16_t peek_indent(Scanner *scanner) {
  if (scanner->size == 0)
    return 0;
  return scanner->indent_stack[scanner->size - 1];
}

bool tree_sitter_koskari_external_scanner_scan(void *payload, TSLexer *lexer,
                                               const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;

  uint32_t indent_length = 0;
  bool found_newline = false;

  for (;;) {
    if (lexer->lookahead == '\n') {
      found_newline = true;
      indent_length = 0;
      skip(lexer);
    } else if (lexer->lookahead == ' ') {
      indent_length++;
      skip(lexer);
    } else if (lexer->lookahead == '\t') {
      indent_length += 4;
      skip(lexer);
    } else if (lexer->lookahead == '\r' || lexer->lookahead == '\f') {
      indent_length = 0;
      skip(lexer);
    } else if (lexer->lookahead == ';') {
      while (lexer->lookahead && lexer->lookahead != '\n') {
        skip(lexer);
      }
      if (lexer->lookahead == '\n') {
        skip(lexer);
        indent_length = 0;
      }
    } else if (lexer->eof(lexer)) {
      indent_length = 0;
      found_newline = true;
      break;
    } else {
      break;
    }
  }

  if (found_newline && scanner->size > 0) {
    uint16_t current = peek_indent(scanner);

    if (valid_symbols[INDENT] && indent_length > current) {
      push_indent(scanner, indent_length);
      lexer->result_symbol = INDENT;
      return true;
    }

    if (valid_symbols[DEDENT] && indent_length < current) {
      pop_indent(scanner);
      lexer->result_symbol = DEDENT;
      return true;
    }
  }

  return false;
}

void *tree_sitter_koskari_external_scanner_create(void) {
  Scanner *scanner = calloc(1, sizeof(Scanner));
  scanner->capacity = CAPACITY;
  scanner->indent_stack = calloc(CAPACITY, sizeof(uint16_t));
  scanner->size = 0;
  push_indent(scanner, 0);
  return scanner;
}

void tree_sitter_koskari_external_scanner_destroy(void *payload) {
  Scanner *scanner = (Scanner *)payload;
  free(scanner->indent_stack);
  free(scanner);
}

unsigned tree_sitter_koskari_external_scanner_serialize(void *payload,
                                                       char *buffer) {
  Scanner *scanner = (Scanner *)payload;
  uint32_t size = 0;

  if (scanner->size > 255)
    scanner->size = 255;

  buffer[size++] = (char)scanner->size;

  for (uint32_t i = 0; i < scanner->size && size < TREE_SITTER_SERIALIZATION_BUFFER_SIZE - 2; i++) {
    uint16_t v = scanner->indent_stack[i];
    buffer[size++] = (char)(v & 0xFF);
    buffer[size++] = (char)((v >> 8) & 0xFF);
  }

  return size;
}

void tree_sitter_koskari_external_scanner_deserialize(void *payload,
                                                      const char *buffer,
                                                      unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  scanner->size = 0;

  if (length > 0) {
    uint8_t count = (uint8_t)buffer[0];
    for (uint8_t i = 0; i < count && (2 + i * 2 + 1) <= length; i++) {
      uint16_t v = (unsigned char)buffer[1 + i * 2] |
                   ((unsigned char)buffer[2 + i * 2] << 8);
      push_indent(scanner, v);
    }
  }

  if (scanner->size == 0) {
    push_indent(scanner, 0);
  }
}
