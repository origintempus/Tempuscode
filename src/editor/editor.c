//
// File: editor.c                      -- Part of TempusMUD
//
// All modifications and additions are
// Copyright 1998 by John Watson, all rights reserved.
//
//

#ifdef HAS_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <fcntl.h>
// Tempus Includes
#include "screen.h"
#include "desc_data.h"
#include "comm.h"
#include "db.h"
#include "utils.h"
#include "login.h"
#include "interpreter.h"
#include "boards.h"
#include "mail.h"
#include "editor.h"
#include "tmpstr.h"
#include "accstr.h"
#include "help.h"
#include "comm.h"

extern struct descriptor_data *descriptor_list;
extern struct help_collection *Help;

void
editor_import(struct editor *editor, const char *text)
{
    gchar **strv, **strp;

    g_list_free(editor->original);
    g_list_free(editor->lines);

    strv = g_strsplit(text, "\n", 0);
    for (strp = strv;*strp;strp++) {
        editor->original = g_list_prepend(editor->original,
                                          g_string_new(*strp));
        editor->lines = g_list_prepend(editor->lines,
                                       g_string_new(*strp));
    }
    g_freestrv(strv);

    g_list_reverse(editor->original);
    g_list_reverse(editor->lines);
}

void
editor_emit(struct editor *editor, char *text)
{
    send_to_desc(editor->desc, "%s", text);
}

void
emit_editor_startup(struct editor *editor)
{
    int i;

    send_to_desc(editor->desc,
                 "&C     * &YTEDII &b]&n Save and exit with @ on a new line. "
                 "&&H for help             &C*\r\n");
    send_to_desc(editor->desc, "     ");
	for (i = 0; i < 7; i++)
        send_to_desc(editor->desc, "&C%d&B---------", i);
    send_to_desc(editor->desc, "&C7&n\r\n");
}

void
editor_send_prompt(struct editor *editor)
{
    send_to_desc(editor->desc, "%3zd&b]&n ", editor_line_count(editor) + 1);
}

int
editor_buffer_size(struct editor *editor)
{
    int len;

    void count_string_len(GString *str, gpointer ignore) {
        len += str->len + 1;
    }
    g_list_foreach(editor->lines, count_string_len, 0);

    return len;
}

void
editor_finish(struct editor *editor, bool save)
{
    struct descriptor_data *desc = editor->desc;

    if (save) {
        int length;
        char *text, *write_pt;

        length = editor_buffer_size(editor);
        text = (char *)malloc(length);
        strcpy(text, "");
        write_pt = text;
        void concat_line(GString *line, gpointer ignore) {
            strcpy(write_pt, line->str);
            write_pt += line->len;
        }
        g_list_foreach(editor->lines, concat_line, 0);

        // Call the finalizer
        editor->finalize(editor, text);

        free(text);
    } else {
        editor->cancel(editor);
    }

	if (IS_PLAYING(desc) && desc->creature && !IS_NPC(desc->creature)) {
        // Remove "using the editor bits"
        REMOVE_BIT(PLR_FLAGS(desc->creature),
                   PLR_WRITING | PLR_OLC | PLR_MAILING);
        REMOVE_BIT(PRF2_FLAGS(desc->creature), PRF2_NOWRAP);
	}

    // Free the editor
	desc->text_editor = NULL;
    g_list_foreach(editor->original, g_string_free, true);
    g_list_free(editor->original);
    g_list_foreach(editor->lines, g_string_free, true);
    g_list_free(editor->lines);
	free(editor);
}

void
editor_finalize(struct editor *editor, char *text)
{
    // Do nothing
}

void
editor_cancel(struct editor *editor)
{
    // Do nothing
}

void
editor_handle_input(struct editor *editor, char *input)
{
	// 2 special chars, @ and &
	char inbuf[MAX_INPUT_LENGTH + 1];
    char *args;

	strncpy(inbuf, input, MAX_INPUT_LENGTH);

	if (*inbuf == '&') {		// Commands
        if (!inbuf[1]) {
            SendMessage("& is the command character. Type &h for help.\r\n");
            return;
        }
        args = inbuf + 2;
        while (*args && isspace(*args))
            args++;
        if (!editor->do_command(editor, inbuf[1], args))
            editor_emit(editor, "Invalid Command. Type &h for help.\r\n");
	} else if (*inbuf == '@') {	// Finish up
        editor_finish(editor, true);
	} else {					// Dump the text in
		editor_append(editor, inbuf);
	}
}

void
editor_display(struct editor *editor, int start_line, int line_count)
{
    unsigned int linenum, end_line;
    GList *itr;

    acc_string_clear();

    // Calculate last line number we're going to show
    end_line = editor_line_count(editor) + 1;
    if (line_count >= 0 && start_line + line_count < end_line)
        end_line = start_line + line_count;

    // Set the iterator to the beginning line
    itr = g_list_nth(editor->lines, start_line - 1);

    // Display the lines from the beginning line to the end, making
    // sure we don't overflow the LARGE_BUF desc buffer
	for (linenum = start_line;linenum < end_line;linenum++, itr = g_list_next(itr)) {
		acc_sprintf("%3d%s%s]%s %s\r\n", linenum,
                    CCBLD(editor->desc->creature, C_CMP),
                    CCBLU(editor->desc->creature, C_NRM),
                    CCNRM(editor->desc->creature, C_NRM),
                    ((GString *)itr->data)->str);
		if (acc_get_length() > (LARGE_BUFSIZE - 1024))
			break;
	}

    acc_strcat("\r\n", NULL);
	if (acc_get_length() > (LARGE_BUFSIZE - 1024))
        acc_strcat("Output buffer limit reached. Use \"&r <line number>\" to specify starting line.\r\n", NULL);

	editor_emit(editor, acc_get_string());
}

bool
editor_is_full(struct editor *editor, char *line)
{
    return (strlen(line) + editor_buffer_size(editor) > editor->max_size);
}

gint
editor_line_count(struct editor *editor)
{
    return editor_line_count(editor);
}

void
editor_append(struct editor *editor, char *line)
{
    if (editor_is_full(editor, line)) {
        editor_emit(editor, "Error: The buffer is full.\r\n");
        return;
    }
    if (PLR_FLAGGED(editor->desc->creature, PLR_OLC))
        g_strdelimit(line, "~", '?');
    g_list_append(editor->lines, g_string_new(line));
}

bool
editor_insert(struct editor *editor, int lineno, char *line)
{
	GList *s;

    if (lineno < 1)
        lineno = 1;
	if (*line)
		line++;

	if (line > editor_line_count(editor)) {
		editor_emit(editor,
                    "You can't insert before a line that doesn't exist.\r\n");
		return false;
	}
	if (editor_is_full(editor, line)) {
		editor_emit(editor, "Error: The buffer is full.\r\n");
		return false;
	}


    s = g_list_nth(editor->lines, lineno - 1);
    editor->lines = g_list_insert_before(editor->lines, s, g_string_new(line));

	return true;
}

bool
editor_replace_line(struct editor *editor, unsigned int lineno, char *line)
{
    GList *s;

	if (*line && *line == ' ')
		line++;

	if (line < 1 || line > editor_line_count(editor)) {
		editor_emit(editor, "There's no line to replace there.\r\n");
		return false;
	}
	// Find the line
    s = g_list_nth(editor->lines, lineno - 1);
    advance(s, line - 1);

	// Make sure we can fit the new stuff in
    if (editor_is_full(editor, line)) {
		editor_emit(editor, "Error: The buffer is full.\r\n");
		return false;
	}
    g_string_assign(s->data, line);

	return true;
}

bool
editor_movelines(struct editor *editor,
                 unsigned int start_line,
                 unsigned int end_line,
                 unsigned int dest_line)
{
    GList *dest, *begin, *end, *next;
    GList *cur;

    if (start_line < 1 || start_line > editor_line_count(editor)) {
        if (start_line == end_line)
            editor_emit(editor, "Line %d is an invalid line.\r\n");
        else
            editor_emit(editor, "Starting line %d is an invalid line.\r\n");
        return false;
    }
    if (end_line < 1 || end_line > editor_line_count(editor)) {
        editor_emit(editor, "Ending line %d is an invalid line.\r\n");
        return false;
    }

    // Nothing needs to be done if destination is inside the block
    if (dest_line >= start_line && dest_line <= end_line)
        return true;

    if (dest_line < 1)
        dest_line = 1;
    if (dest_line >= editor_line_count(editor))
        dest_line = editor_line_count(editor) + 1;

    begin = g_list_nth(editor->lines, start_line);
    end = g_list_nth(editor->lines, end_line + 1);
    dest = g_list_nth(editor->lines, dest_line);

    for (cur = begin;cur != end;cur = next) {
        next = cur->next;
        editor->lines = g_list_insert_before(editor->lines, dest, cur->data);
        editor->lines = g_list_delete_link(editor->lines, cur);
    }

    if (start_line == end_line) {
        editor_emit(editor, tmp_sprintf("Moved line %d above line %d.\r\n",
                                start_line, dest_line));
    } else {
        editor_emit(editor, tmp_sprintf("Moved lines %d-%d to the line above line %d.\r\n",
                                start_line, end_line, dest_line));
    }

    return true;
}

bool
editor_find(struct editor *editor, char *args)
{
    int i = 0;

    void print_if_match(GString *str, gpointer ignore) {
        i++;
        if (strcasestr(str, args))
            acc_sprintf("%3d%s%s]%s %s\r\n", i,
                    CCBLD(editor->desc->creature, C_CMP),
                    CCBLU(editor->desc->creature, C_NRM),
                    CCNRM(editor->desc->creature, C_NRM),
                    str->str);
    }

    acc_string_clear();
    g_list_foreach(editor->lines, print_if_match, 0);
    acc_strcat("\r\n", NULL);
    editor_emit(editor, acc_get_string());

    return true;
}

bool
editor_substitute(struct editor *editor, char *args)
{
    const char *usage = "There are two formats for substitute:\r\n  &s [search pattern] [replacement]\r\n  &s /search pattern/replacement/\r\nIn the first form, you can use (), [], <>, or {}.\r\n";
	// Iterator to the current line in theText
	GList *line;
	// The string containing the search pattern
	char *pattern;
    int pattern_len;
	// String containing the replace pattern
	char *replacement;
    int replacement_len;
	// Number of replacements made
	int replaced = 0;
	// read pointer and write pointer.
	char *temp, end_char;
    bool balanced;
    int size_delta, buffer_size;;

    while (isspace(*args))
        args++;

	if (!*args) {
		editor_emit(editor, usage);
		return false;
	}

    // First non-space character is the search delimiter.  If the
    // delimiter is something used with some kind of brace, it
    // terminates in the opposite brace
    balanced = true;
    switch (*args) {
    case '(':
        end_char = ')'; break;
    case '[':
        end_char = ']'; break;
    case '{':
        end_char = '}'; break;
    case '<':
        end_char = '>'; break;
    default:
        end_char = *args;
        balanced = false;
        break;
    }
    args++;

    temp = args;
    pattern = args;
    while (*args && *args != end_char)
        args++;
    if (!*args) {
        editor_emit(editor, usage);
        return false;
    }

    pattern_len = args - temp;

    // If the pattern was specified with a balanced delimiter, then
    // the replacement must be, too
    if (balanced) {
        while (*args
               && *args != '('
               && *args != '['
               && *args != '{'
               && *args != '<')
            args++;
        if (!*args) {
            editor_emit(editor, "If the search pattern uses a balanced delimiter, the replacement must use a balanced\r\ndelimiter as well.\r\n");
            return false;
        }

        switch (*args) {
        case '(':
            end_char = ')'; break;
        case '[':
            end_char = ']'; break;
        case '{':
            end_char = '}'; break;
        case '<':
            end_char = '>'; break;
            break;
        default:
            end_char = *args; break;
        }
        args++;
    }

    temp = args;
    replacement = temp;

    while (*args && *args != end_char)
        args++;

    replacement_len = args - temp;

    size_delta = strlen(replacement) - strlen(pattern);
    buffer_size = editor_buffer_size(editor);

	// Find pattern in theText a line at a time and replace each instance.
	char *pos;			// Current position in the line
	bool overflow = false;		// Have we overflowed the buffer?

    for (line = editor->lines;
         line && buffer_size + size_delta < editor->max_size;
         line = line->next) {
        pos = strstr(((GString *)line->data)->str, pattern);
        while (pos && buffer_size + size_delta < editor->max_size) {
            g_string_erase((GString *)line->data,
                           pos - ((GString *)line->data)->str,
                           pattern_len);
            g_string_insert_len((GString *)line->data,
                                pos - ((GString *)line->data)->str,
                                replacement,
                                replacement_len);
            replaced++;
            buffer_size += size_delta;
            pos = strstr(pos + replacement_len, pattern);
        }
    }
	if (buffer_size + size_delta > editor->max_size)
        editor_emit(editor, "Error: The buffer is full.\r\n");
    if (replaced > 0) {
        editor_emit(editor, tmp_sprintf(
                        "Replaced %d occurrence%s of '%s' with '%s'.\r\n",
                        replaced,
                        (replaced == 1) ? "":"s",
                        pattern,
                        replacement));
    } else {
        editor_emit(editor, "Search string not found.\r\n");
    }

	return true;
}

bool
editor_wrap(struct editor *editor)
{
    GString *tempstr;
    GList *line, *new_line;
    char *s;
	int linebreak;

	for (line = editor->lines; line; line = line->next) {
		linebreak = 76;

        // If its less than 77 chars, it don't need ta be wrapped.
		if (((GString *)line->data)->len <= linebreak)
			continue;

		s = ((GString *)line->data)->str;

		// Find the first space <= 76th char.
		for (s += linebreak - 1;
             s != ((GString *)line->data)->str && !isspace(*s);
             s--)
			linebreak--;

		if (linebreak == 1) {	// Linebreak is at 76
            s = ((GString *)line->data)->str;
			s += 75;
		}

        new_line = g_string_new((linebreak > 1) ? s + 1:s);
		g_string_truncate(((GString *)line->data),
                          s - ((GString *)line->data)->str);
		line = line->next;
        editor->lines = g_list_insert_before(editor->lines,
                                             line,
                                             new_line);
		line = editor->lines;
	}
	return true;
}

bool
editor_remove(struct editor *editor,
              unsigned int start_line,
              unsigned int finish_line)
{
	GList *start, *finish, *next;

	if (start_line < 1 || start_line > editor_line_count(editor)) {
		editor_emit(editor, "Someone already deleted that line boss.\r\n");
		return false;
	}
	if (finish_line < 1 || finish_line > editor_line_count(editor)) {
		editor_emit(editor, "Someone already deleted that line boss.\r\n");
		return false;
	}

    start = g_list_nth(editor->lines, start_line -  1);
    finish = g_list_nth(editor->lines, finish_line -  1);
    while (start != finish) {
        next = start->next;
        g_string_free((GString *)start->data, true);
        editor->lines = g_list_delete_link(editor->lines, start);
        start = next;
    }

    if (start_line == finish_line)
        editor_emit(editor, tmp_sprintf("Line %d deleted.\r\n", start_line));
    else
        editor_emit(editor, tmp_sprintf("Lines %d-%d deleted.\r\n",
                                start_line,
                                finish_line));

	return true;
}

bool
editor_clear(struct editor *editor)
{
    g_list_foreach(editor->lines, g_string_free, true);
    g_list_free(editor->lines);
    editor->lines = NULL;

	return true;
}

void
editor_undo(struct editor *editor)
{
	if (editor->original) {
        editor_clear(editor);
        GList *line;

        for (line = editor->original;line;line = line->next) {
            GString *new_line = g_string_new_len(((GString *)line->data)->str,
                                                 ((GString *)line->data)->len);
            editor->lines = g_list_prepend(editor->lines, new_line);
        }

        g_list_reverse(editor->lines);

        editor_emit(editor, "Original buffer restored.\r\n");
	} else {
		editor_emit(editor, "There's no original to undo to.\r\n");
    }
}

void
editor_sendmodalhelp(struct editor *editor)
{
    // default offers the clear buffer and undo changes options
    send_to_desc(editor->desc, "            &YC - &nClear Buffer         &YU - &nUndo Changes  \r\n");
}

void
editor_help(struct editor *editor, char *line)
{
	char command[MAX_INPUT_LENGTH];

    acc_string_clear();

	if (!*line) {
        send_to_desc(editor->desc,
                     "     &C*&B-----------------------&Y H E L P &B-----------------------&C*\r\n"
                     "            &YR - &nRefresh Screen       &YH - &nHelp         \r\n"
                     "            &YE - &nSave and Exit        &YQ - &nQuit (Cancel)\r\n"
                     "            &YL - &nReplace Line         &YD - &nDelete Line  \r\n"
                     "            &YI - &nInsert Line          &YM - &nMove Line(s)\r\n"
                     "            &YF - &nFind                 &YS - &nSubstitute\r\n");
        editor->sendmodalhelp(editor);
        send_to_desc(editor->desc,
                     "     &C*&B-------------------------------------------------------&C*&n\r\n");
	} else {
        struct help_item *help_item;

		line = one_argument(line, command);
		*command = tolower(*command);
        help_item = help_finditems(tmp_sprintf("tedii-%c", *command),
                                   false, 0, false);
        if (help_item) {
            help_item_load_text(help_item);
            send_to_desc(editor->desc, "&cTEDII Command '%c'&n\r\n", *command);
            send_to_desc(editor->desc, help_item->text);
        } else {
            send_to_desc(editor->desc, "Sorry.  There is no help on that.\r\n");
        }
    }
}

bool
parse_optional_range(const char *arg, int *start, int *finish)
{
    const char *dash = strchr(arg, '-');

    if (dash) {
        char *str;

        // Parse range
        str = tmp_substr(arg, 0, dash - arg - 1);
        if (!isnumber(str))
            return false;
        *start = atoi(str);

        str = tmp_substr(arg, dash - arg + 1, -1);
        if (!isnumber(str))
            return false;
        *finish = atoi(str);

        if (start > finish) {
            int tmp = *finish;

            *finish = *start;
            *start = tmp;
        }

        return true;
    }

    // Ensure single arg is numeric
    if (!isnumber(arg))
        return false;

    // Single number
    *start = *finish = atoi(arg);
    return true;
}

bool
editor_do_command(struct editor *editor, char cmd, char *args)
{
	int line, start_line, end_line, dest_line;
	char command[MAX_INPUT_LENGTH];

	switch (tolower(cmd)) {
	case 'h':					// Help
		editor_help(editor, args);
        break;
	case 'e':					// Save and Exit
        editor_finish(editor, true);
		break;
	case 'q':					// Quit without saving
        editor_finish(editor, false);
		break;
	case 's':					// Substitute
        editor_substitute(editor, args);
		break;
    case 'f':                   // Find
        editor_find(editor, args);
        break;
	case 'c':					// Clear Buffer
		editor_clear(editor);
		editor_emit(editor, "Cleared.\r\n");
		break;
	case 'l':					// Replace Line
		args = one_argument(args, command);
		if (!isdigit(*command)) {
			editor_emit(editor, "Format for Replace Line is: &l <line #> <text>\r\n");
			break;
		}
		line = atoi(command);
		if (line < 1) {
			editor_emit(editor, "Format for Replace Line is: &l <line #> <text>\r\n");
			break;
		}
        editor_replaceline(line, args);
		break;
	case 'i':					// Insert Line
		args = one_argument(args, command);
		if (!isdigit(*command)) {
			SendMessage
				("Format for insert command is: &i <line #> <text>\r\n");
			break;
		}
		line = atoi(command);
		if (line < 1) {
			editor_emit(editor, "Format for insert command is: &i <line #><text>\r\n");
			break;
		}
        editor_insert(editor, line, args);
		break;
	case 'd':					// Delete Line
		args = one_argument(args, command);
        if (!parse_optional_range(command, &start_line, &end_line)) {
			editor_emit(editor, "Format for delete command is: &d <line #>\r\n");
			break;
		}
        editor_remove(editor, start_line, end_line);
		break;
    case 'm': {
        char *arg;

        arg = tmp_getword(&args);
        if (!*arg) {
            editor_emit(editor, "Format for move command is: &m (<start line>-<end line>|<linenum>) <destination>\r\n");
            break;
        }

        if (!parse_optional_range(arg, &start_line, &end_line)) {
            editor_emit(editor, "Format for move command is: &m (<start line>-<end line>|<linenum>) <destination>\r\n");
            break;
        }

        arg = tmp_getword(&args);
        if (!*arg || !isnumber(arg)) {
            editor_emit(editor, "Format for move command is: &m (<start line>-<end line>|<line #>) <destination>\r\n");
            break;
        }
        dest_line = atoi(arg);

        if (dest_line >= start_line && dest_line <= end_line + 1) {
            editor_emit(editor, "The line range you specified is already at the destination.\r\n");
            break;
        }

        editor_movelines(editor, start_line, end_line, dest_line);
        break;
    }
	case 'r':					// Refresh Screen
        if (!*args) {
            editor_display(editor, 0, -1);
        } else if (isnumber(args) && (line = atoi(args)) > 0) {
            int line_count = editor->desc->account->term_height;

            editor_display(editor, MAX(1, line - line_count / 2), line_count);
		} else {
            editor_emit(editor, "Format for refresh command is: &r [<line #>]\r\nOmit line number to display the whole buffer.\r\n");
        }
		break;
	default:
		return false;
	}

	return true;
}

struct editor *
make_editor(struct descriptor_data *d, int max)
{
    struct editor *editor;

    CREATE(editor, struct editor, 1);
    editor->desc = d;
    editor->max_size = max;
    editor->do_command = editor_do_command;
    editor->finalize = editor_finalize;
    editor->cancel = editor_cancel;
    editor->displaybuffer = editor_display;
    editor->sendmodalhelp = editor_sendmodalhelp;

    return editor;
}