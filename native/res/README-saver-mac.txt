PRISMATICA RIPPLE TANK SCREEN SAVER (macOS 10.15+, Intel and Apple Silicon)
A calm, slow version of the Ripple Tank scene. No music, no sound, no settings.

TWO VERSIONS - install either or both, they don't conflict:
  Prismatica Ripple Tank.saver      renders at 1x (standard resolution). Lightest.
  Prismatica Ripple Tank HD.saver   renders at 1.5x on Retina screens, then scales up. Sharper,
                                    about 2.25x the GPU work of the regular one.

UPDATING FROM AN EARLIER VERSION (so the new thumbnail shows up)
  1. In System Settings -> Screen Saver, right-click the old Prismatica savers -> Delete
     (or delete them from ~/Library/Screen Savers). Remove "Prismatica Ripple Tank Retina" too.
  2. Quit System Settings completely.
  3. Double-click the new .saver files and choose "Install for this user only".

INSTALL   Double-click a .saver file and choose "Install for this user only".
          It appears under System Settings -> Screen Saver (in the "Other" group at the bottom).

IF IT SHOWS A BLACK PREVIEW OR macOS SAYS IT CAN'T BE OPENED
          They aren't notarized. Clear the download flag in Terminal, then reopen System Settings:
            xattr -cr ~/Library/Screen\ Savers/Prismatica*.saver
