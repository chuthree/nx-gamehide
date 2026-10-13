# Troubleshooting

## Games do not hide or reappear

Close the game, exit Tesla and check HOME again. If the sysmodule is offline or changes still do not appear, extract the full installation package again and fully reboot.

## Disable the plugin

Create an empty `config/nx-gamehide/disable.flag` on the SD card and fully reboot.

If the console cannot boot, use a card reader or Hekate UMS to move `atmosphere/contents/00FF48474D000001` out of `contents`, then boot again.

## Hidden list and recovery

The list is saved in `config/nx-gamehide/hidden_games.txt`. Use the plugin to edit it. If editing manually, close the plugin first; use one 16-digit hexadecimal base application ID per line, up to 4096 entries.

If saving was interrupted, back up the config directory and check `hidden_games.txt.bak` and `hidden_games.txt.old`. Restore the correct list as `hidden_games.txt` before reopening the plugin.

## Reporting a problem

When reporting a problem, include system and plugin versions, steps to reproduce, and `boot.log` / `boot.previous.log` from the config directory. Remove personal information from logs.

The multilingual UI has not yet been verified on a console.
