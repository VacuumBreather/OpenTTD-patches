/* $Id$ */

/** @file osk_gui.cpp The On Screen Keyboard GUI */

#include "stdafx.h"
#include "openttd.h"

#include "textbuf_gui.h"
#include "window_gui.h"
#include "string_func.h"
#include "strings_func.h"
#include "debug.h"
#include "window_func.h"
#include "gfx_func.h"
#include "querystring_gui.h"

#include "table/sprites.h"
#include "table/strings.h"

enum OskWidgets {
	OSK_WIDGET_TEXT = 3,
	OSK_WIDGET_CANCEL = 5,
	OSK_WIDGET_OK,
	OSK_WIDGET_BACKSPACE,
	OSK_WIDGET_SPECIAL,
	OSK_WIDGET_CAPS,
	OSK_WIDGET_SHIFT,
	OSK_WIDGET_SPACE,
	OSK_WIDGET_LEFT,
	OSK_WIDGET_RIGHT,
	OSK_WIDGET_LETTERS
};

char _keyboard_opt[2][OSK_KEYBOARD_ENTRIES * 4 + 1];
static WChar _keyboard[2][OSK_KEYBOARD_ENTRIES];

enum {
	KEYS_NONE,
	KEYS_SHIFT,
	KEYS_CAPS
};
static byte _keystate = KEYS_NONE;

struct OskWindow : public Window {
	QueryString *qs;       ///< text-input
	int text_btn;          ///< widget number of parent's text field
	int ok_btn;            ///< widget number of parent's ok button (=0 when ok shouldn't be passed on)
	int cancel_btn;        ///< widget number of parent's cancel button (=0 when cancel shouldn't be passed on; text will be reverted to original)
	Textbuf *text;         ///< pointer to parent's textbuffer (to update caret position)
	char orig_str_buf[64]; ///< Original string.

	OskWindow(const WindowDesc *desc, QueryStringBaseWindow *parent, int button, int cancel, int ok) : Window(desc)
	{
		this->parent = parent;
		assert(parent != NULL);

		if (parent->widget[button].data != 0) parent->caption = parent->widget[button].data;

		this->qs         = parent;
		this->text_btn   = button;
		this->cancel_btn = cancel;
		this->ok_btn     = ok;
		this->text       = &parent->text;

		/* make a copy in case we need to reset later */
		strcpy(this->orig_str_buf, this->qs->text.buf);

		SetBit(_no_scroll, SCROLL_EDIT);
		/* Not needed by default. */
		this->DisableWidget(OSK_WIDGET_SPECIAL);

		this->FindWindowPlacementAndResize(desc);
	}

	/**
	 * Only show valid characters; do not show characters that would
	 * only insert a space when we have a spacebar to do that or
	 * characters that are not allowed to be entered.
	 */
	void ChangeOskDiabledState(bool shift)
	{
		for (uint i = 0; i < OSK_KEYBOARD_ENTRIES; i++) {
			this->SetWidgetDisabledState(OSK_WIDGET_LETTERS + i,
					!IsValidChar(_keyboard[shift][i], this->qs->afilter) || _keyboard[shift][i] == ' ');
		}
		this->SetWidgetDisabledState(OSK_WIDGET_SPACE, !IsValidChar(' ', this->qs->afilter));
	}

	virtual void OnPaint()
	{
		bool shift = HasBit(_keystate, KEYS_CAPS) ^ HasBit(_keystate, KEYS_SHIFT);

		this->LowerWidget(OSK_WIDGET_TEXT);
		this->SetWidgetLoweredState(OSK_WIDGET_SHIFT, HasBit(_keystate, KEYS_SHIFT));
		this->SetWidgetLoweredState(OSK_WIDGET_CAPS, HasBit(_keystate, KEYS_CAPS));

		this->ChangeOskDiabledState(shift);

		SetDParam(0, this->qs->caption);
		DrawWindowWidgets(this);

		for (uint i = 0; i < OSK_KEYBOARD_ENTRIES; i++) {
			DrawCharCentered(_keyboard[shift][i],
				this->widget[OSK_WIDGET_LETTERS + i].left + 8,
				this->widget[OSK_WIDGET_LETTERS + i].top + 3,
				TC_BLACK);
		}

		this->qs->DrawEditBox(this, OSK_WIDGET_TEXT);
	}

	virtual void OnClick(Point pt, int widget)
	{
		/* clicked a letter */
		if (widget >= OSK_WIDGET_LETTERS) {
			bool shift = HasBit(_keystate, KEYS_CAPS) ^ HasBit(_keystate, KEYS_SHIFT);

			WChar c = _keyboard[shift][widget - OSK_WIDGET_LETTERS];

			if (!IsValidChar(c, this->qs->afilter)) return;

			if (InsertTextBufferChar(&this->qs->text, c)) this->InvalidateWidget(OSK_WIDGET_TEXT);

			if (HasBit(_keystate, KEYS_SHIFT)) {
				ToggleBit(_keystate, KEYS_SHIFT);
				this->widget[OSK_WIDGET_SHIFT].color = HasBit(_keystate, KEYS_SHIFT) ? 15 : 14;
				this->SetDirty();
			}
			return;
		}

		bool delete_this = false;

		switch (widget) {
			case OSK_WIDGET_BACKSPACE:
				if (DeleteTextBufferChar(&this->qs->text, WKC_BACKSPACE)) this->InvalidateWidget(OSK_WIDGET_TEXT);
				break;

			case OSK_WIDGET_SPECIAL:
				/*
				 * Anything device specific can go here.
				 * The button itself is hidden by default, and when you need it you
				 * can not hide it in the create event.
				 */
				break;

			case OSK_WIDGET_CAPS:
				ToggleBit(_keystate, KEYS_CAPS);
				this->SetDirty();
				break;

			case OSK_WIDGET_SHIFT:
				ToggleBit(_keystate, KEYS_SHIFT);
				this->SetDirty();
				break;

			case OSK_WIDGET_SPACE:
				if (InsertTextBufferChar(&this->qs->text, ' ')) this->InvalidateWidget(OSK_WIDGET_TEXT);
				break;

			case OSK_WIDGET_LEFT:
				if (MoveTextBufferPos(&this->qs->text, WKC_LEFT)) this->InvalidateWidget(OSK_WIDGET_TEXT);
				break;

			case OSK_WIDGET_RIGHT:
				if (MoveTextBufferPos(&this->qs->text, WKC_RIGHT)) this->InvalidateWidget(OSK_WIDGET_TEXT);
				break;

			case OSK_WIDGET_OK:
				if (this->qs->orig == NULL || strcmp(this->qs->text.buf, this->qs->orig) != 0) {
					/* pass information by simulating a button press on parent window */
					if (this->ok_btn != 0) {
						this->parent->OnClick(pt, this->ok_btn);
						/* Window gets deleted when the parent window removes itself. */
						return;
					}
				}
				delete_this = true;
				break;

			case OSK_WIDGET_CANCEL:
				if (this->cancel_btn != 0) { // pass a cancel event to the parent window
					this->parent->OnClick(pt, this->cancel_btn);
					/* Window gets deleted when the parent window removes itself. */
					return;
				} else { // or reset to original string
					strcpy(qs->text.buf, this->orig_str_buf);
					UpdateTextBufferSize(&qs->text);
					MoveTextBufferPos(&qs->text, WKC_END);
					delete_this = true;
				}
				break;
		}
		/* make sure that the parent window's textbox also gets updated */
		if (this->parent != NULL) this->parent->InvalidateWidget(this->text_btn);
		if (delete_this) delete this;
	}

	virtual void OnMouseLoop()
	{
		this->qs->HandleEditBox(this, OSK_WIDGET_TEXT);
		/* make the caret of the parent window also blink */
		this->parent->InvalidateWidget(this->text_btn);
	}
};

static const Widget _osk_widgets[] = {
{      WWT_EMPTY, RESIZE_NONE,     0,     0,     0,     0,     0, 0x0,               STR_NULL},
{    WWT_CAPTION, RESIZE_NONE,    14,     0,   255,     0,    13, STR_012D,          STR_NULL},
{      WWT_PANEL, RESIZE_NONE,    14,     0,   255,    14,    29, 0x0,               STR_NULL},
{    WWT_EDITBOX, RESIZE_NONE,    14,     2,   253,    16,    27, 0x0,               STR_NULL},

{      WWT_PANEL, RESIZE_NONE,    14,     0,   255,    30,   139, 0x0,               STR_NULL},

{    WWT_TEXTBTN, RESIZE_NONE,    14,     3,   108,    35,    46, STR_012E_CANCEL,   STR_NULL},
{    WWT_TEXTBTN, RESIZE_NONE,    14,   111,   216,    35,    46, STR_012F_OK,       STR_NULL},
{ WWT_PUSHIMGBTN, RESIZE_NONE,    14,   219,   252,    35,    46, SPR_OSK_BACKSPACE, STR_NULL},

{ WWT_PUSHIMGBTN, RESIZE_NONE,    14,     3,    27,    67,    82, SPR_OSK_SPECIAL,   STR_NULL},
{     WWT_IMGBTN, RESIZE_NONE,    14,     3,    36,    85,   100, SPR_OSK_CAPS,      STR_NULL},
{     WWT_IMGBTN, RESIZE_NONE,    14,     3,    27,   103,   118, SPR_OSK_SHIFT,     STR_NULL},

{ WWT_PUSHTXTBTN, RESIZE_NONE,    14,    75,   189,   121,   136, STR_EMPTY,         STR_NULL},

{ WWT_PUSHIMGBTN, RESIZE_NONE,    14,   219,   234,   121,   136, SPR_OSK_LEFT,      STR_NULL},
{ WWT_PUSHIMGBTN, RESIZE_NONE,    14,   237,   252,   121,   136, SPR_OSK_RIGHT,     STR_NULL},

{    WWT_PUSHBTN, RESIZE_NONE,    14,     3,    18,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    21,    36,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    39,    54,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    57,    72,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    75,    90,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    93,   108,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   111,   126,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   129,   144,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   147,   162,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   165,   180,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   183,   198,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   201,   216,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   219,   234,    49,    64, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   237,   252,    49,    64, 0x0,    STR_NULL},

{    WWT_PUSHBTN, RESIZE_NONE,    14,    30,    45,    67,    82, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    48,    63,    67,    82, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    66,    81,    67,    82, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    84,    99,    67,    82, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   102,   117,    67,    82, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   120,   135,    67,    82, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   138,   153,    67,    82, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   156,   171,    67,    82, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   174,   189,    67,    82, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   192,   207,    67,    82, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   210,   225,    67,    82, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   228,   243,    67,    82, 0x0,    STR_NULL},

{    WWT_PUSHBTN, RESIZE_NONE,    14,    39,    54,    85,   100, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    57,    72,    85,   100, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    75,    90,    85,   100, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    93,   108,    85,   100, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   111,   126,    85,   100, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   129,   144,    85,   100, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   147,   162,    85,   100, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   165,   180,    85,   100, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   183,   198,    85,   100, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   201,   216,    85,   100, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   219,   234,    85,   100, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   237,   252,    85,   100, 0x0,    STR_NULL},

{    WWT_PUSHBTN, RESIZE_NONE,    14,    30,    45,   103,   118, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    48,    63,   103,   118, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    66,    81,   103,   118, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,    84,    99,   103,   118, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   102,   117,   103,   118, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   120,   135,   103,   118, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   138,   153,   103,   118, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   156,   171,   103,   118, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   174,   189,   103,   118, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   192,   207,   103,   118, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   210,   225,   103,   118, 0x0,    STR_NULL},
{    WWT_PUSHBTN, RESIZE_NONE,    14,   228,   243,   103,   118, 0x0,    STR_NULL},

{   WIDGETS_END},
};

WindowDesc _osk_desc = {
	WDP_CENTER, WDP_CENTER, 256, 140, 256, 140,
	WC_OSK, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS,
	_osk_widgets,
	NULL
};

/**
 * Retrieve keyboard layout from language string or (if set) config file.
 * Also check for invalid characters.
 */
void GetKeyboardLayout()
{
	char keyboard[2][OSK_KEYBOARD_ENTRIES * 4 + 1];
	char errormark[2][OSK_KEYBOARD_ENTRIES + 1]; // used for marking invalid chars
	bool has_error = false; // true when an invalid char is detected

	if (StrEmpty(_keyboard_opt[0])) {
		GetString(keyboard[0], STR_OSK_KEYBOARD_LAYOUT, lastof(keyboard[0]));
	} else {
		strncpy(keyboard[0], _keyboard_opt[0], lengthof(keyboard[0]));
	}

	if (StrEmpty(_keyboard_opt[1])) {
		GetString(keyboard[1], STR_OSK_KEYBOARD_LAYOUT_CAPS, lastof(keyboard[1]));
	} else {
		strncpy(keyboard[1], _keyboard_opt[1], lengthof(keyboard[1]));
	}

	for (uint j = 0; j < 2; j++) {
		const char *kbd = keyboard[j];
		bool ended = false;
		for (uint i = 0; i < OSK_KEYBOARD_ENTRIES; i++) {
			_keyboard[j][i] = Utf8Consume(&kbd);

			/* Be lenient when the last characters are missing (is quite normal) */
			if (_keyboard[j][i] == '\0' || ended) {
				ended = true;
				_keyboard[j][i] = ' ';
				continue;
			}

			if (IsPrintable(_keyboard[j][i])) {
				errormark[j][i] = ' ';
			} else {
				has_error = true;
				errormark[j][i] = '^';
				_keyboard[j][i] = ' ';
			}
		}
	}

	if (has_error) {
		ShowInfoF("The keyboard layout you selected contains invalid chars. Please check those chars marked with ^.");
		ShowInfoF("Normal keyboard:  %s", keyboard[0]);
		ShowInfoF("                  %s", errormark[0]);
		ShowInfoF("Caps Lock:        %s", keyboard[1]);
		ShowInfoF("                  %s", errormark[1]);
	}
}

/**
 * Show the osk associated with a given textbox
 * @param parent pointer to the Window where this keyboard originated from
 * @param q      querystr_d pointer to the query string of the parent, which is
 *               shared for both windows
 * @param button widget number of parent's textbox
 * @param cancel widget number of parent's cancel button (0 if cancel events
 *               should not be passed)
 * @param ok     widget number of parent's ok button  (0 if ok events should not
 *               be passed)
 */
void ShowOnScreenKeyboard(QueryStringBaseWindow *parent, int button, int cancel, int ok)
{
	DeleteWindowById(WC_OSK, 0);

	GetKeyboardLayout();
	new OskWindow(&_osk_desc, parent, button, cancel, ok);
}
