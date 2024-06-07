#ifndef COMMANDS_H
#define COMMANDS_H

#include <assert.h>
#include <errno.h>
#include <memory.h>
#include <string.h>
#include <unistd.h>

#define COMMAND_MAX_LENGTH 1023

// Copies the contents of from up to `until` into `to` and shifts
// from so that it starts at `from + until`
// Example:
// ```
// from_size = 12
// first_n_chars = 5
// "aaaaabbbbbb"  ""  -->  "bbbbbb" "aaaaa"
//  ^    ^         ^
//  |    |         |
//  |    |         \---to
//  |    \-------------from + first_n_chars
//  \------------------from
// ```
// Note: does not null terminate `to`
void copy_and_shift(char *from, size_t from_size, char *to,
                    size_t first_n_chars) {
  memcpy(to, from, first_n_chars);
  memmove(from, from + first_n_chars, from_size - first_n_chars);
}

/*
 * Appends the first line in `lines` to `line`, and removes it from
 * the `lines` buffer. If what's moved into `line` counts as a full line
 * (meaning it ended with a newline and we didn't just hit the end of the
 * string), then `full_line` will be set to true.
 * If `line` cannot accomodate a full or partial line, then it returns false,
 * while moving what it can from `lines` to `line`, otherwise it returns true
 * and copies the whole line.
 */
bool extract_line(char *lines, char *line, size_t line_size, bool *full_line) {
  size_t line_used = strlen(line);
  size_t lines_len = strlen(lines);
  size_t lines_size = lines_len + 1;
  char *line_free = line + line_used;
  size_t line_free_size = line_size - line_used;

  char *newline_char = strchr(lines, '\n');
  size_t new_line_size =
      newline_char ? (size_t)(newline_char - lines + 1) : (lines_len + 1);

  if (new_line_size > line_free_size) {
    *full_line = false;
    copy_and_shift(lines, lines_size, line_free, line_free_size - 1);
    line[line_size - 1] = '\0';
    return false;
  }

  if (newline_char) {
    *full_line = true;
    *newline_char = '\0';
    copy_and_shift(lines, lines_size, line_free, new_line_size);
  } else {
    *full_line = false;
    line_free[new_line_size - 1] = '\0';
    copy_and_shift(lines, lines_size, line_free, new_line_size - 1);
  }
  return true;
}

bool strlen_safe(const char *str, size_t len_with_null_terminator,
                 size_t *size) {
  const char *end = memchr(str, '\0', len_with_null_terminator);
  if (end == NULL) {
    return false;
  }
  *size = str - end;
  return true;
}

bool read_command(int fd, char *commands_buffer, char *command_buffer,
                  size_t buff_sizes, bool *buffer_full, bool *have_command) {
  size_t command_buff_len, commands_buff_len;
  assert(strlen_safe(command_buffer, buff_sizes, &command_buff_len));
  assert(strlen_safe(commands_buffer, buff_sizes, &commands_buff_len));

  bool full_line = false;
  if (!extract_line(commands_buffer, command_buffer, buff_sizes, &full_line)) {
    *buffer_full = true;
    return true;
  }
  *buffer_full = false;
  if (full_line) {
    *have_command = true;
    return true;
  }
  assert(commands_buffer[0] == '\0');
  int bytes_read = read(fd, commands_buffer, buff_sizes - 1);
  if (bytes_read == -1) {
    if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
      return false;
    }
    *have_command = false;
  }
  commands_buffer[bytes_read] = '\0';

  return true;
}

#endif /* COMMANDS_H */