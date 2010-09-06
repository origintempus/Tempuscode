//
// File: texteditor.cc                        -- part of TempusMUD
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

struct texteditor_data {
    char **target;
};

void
free_texteditor(struct editor *editor)
{
    free(editor->mode_data);
}

void
texteditor_cancel(struct editor *editor)
{
	// If editing their description.
	if (STATE(editor->desc) == CXN_EDIT_DESC) {
		send_to_desc(editor->desc, "\033[H\033[J");
		set_desc_state(CXN_MENU, editor->desc);
	}
    free_texteditor(editor);
}

void
texteditor_finalize(struct editor *editor, const char *text)
{
    struct texteditor_data *text_data = (struct texteditor_data *)editor->mode_data;
    // If we were editing rather than creating, wax what was there.
	if (text_data->target) {
		if (*text_data->target) {
			free(*text_data->target);
			*text_data->target = NULL;
		}
	}

    *text_data->target = strdup(text);

	// If editing their description.
	if (STATE(editor->desc) == CXN_EDIT_DESC) {
		send_to_desc(editor->desc, "\033[H\033[J");
		set_desc_state(CXN_MENU, editor->desc);
        free_texteditor(editor);
		return;
	}

    if (IS_PLAYING(editor->desc)) {
        if (PLR_FLAGGED(editor->desc->creature, PLR_OLC))
            act("$n nods with satisfaction as $e saves $s work.", true, editor->desc->creature, 0, 0, TO_NOTVICT);
        else
            act("$n finishes writing.", true, editor->desc->creature, 0, 0, TO_NOTVICT);
    }
    free_texteditor(editor);
}

void
start_editing_text(struct descriptor_data *d, char **dest, int max)
{
    struct texteditor_data *text_data;
	// There MUST be a destination!
	if (!dest) {
		errlog("NULL destination pointer passed into start_text_editor!!");
		send_to_char(d->creature, "This command seems to be broken. Bug this.\r\n");
		REMOVE_BIT(PLR_FLAGS(d->creature), PLR_WRITING | PLR_OLC | PLR_MAILING);
		return;
	}
	if (d->text_editor) {
		errlog("Text editor object not null in start_text_editor.");
		REMOVE_BIT(PLR_FLAGS(d->creature), PLR_WRITING | PLR_OLC | PLR_MAILING);
		return;
	}
	if (*dest && (strlen(*dest) > (unsigned int)max)) {
		send_to_char(d->creature, "ERROR: Buffer too large for editor.\r\n");
		REMOVE_BIT(PLR_FLAGS(d->creature), PLR_WRITING | PLR_OLC | PLR_MAILING);
		return;
	}

	SET_BIT(PLR_FLAGS(d->creature), PLR_WRITING);

	d->text_editor = make_editor(d, MAX_MAIL_SIZE);
    CREATE(text_data, struct texteditor_data, 1);
    d->text_editor->finalize = texteditor_finalize;
    d->text_editor->cancel = texteditor_cancel;
    d->text_editor->mode_data = text_data;

    text_data->target = dest;
    if (*dest)
        editor_import(d->text_editor, *dest);

    emit_editor_startup(d->text_editor);
    display_buffer(d->text_editor);
}