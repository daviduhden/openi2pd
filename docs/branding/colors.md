# Colors

The canonical numeric palette and all semantic light/dark roles live in
[`assets/branding/colors.md`](../assets/branding/colors.md).

## Core logo colors

| Name | Value | Role |
|---|---|---|
| openi2pd teal | `#00B2B4` | Network/tunnel accent |
| openi2pd blue | `#078DE4` | Interactive/action accent |
| openi2pd navy | `#162737` | Text and dark surfaces |
| openi2pd white | `#FFFFFF` | Node highlights and light surfaces |

The sampled teal and blue are graphic accents. Do not use them as normal-size
text on white; use the semantic `primary`/`secondary` tokens from the canonical
file. That file also records the light/dark error, warning, success, selection,
focus, disabled and surface roles, with contrast ratios.

Android XML, the ncurses theme and documentation CSS map onto those semantic
tokens. They must not introduce independent brand hex values.
