// Who the game is (reWild B0): one place for what LASRecomp spread over the launcher, kernel, paths and platform code.
#pragma once

#define GAME_APP_NAME      "reWild"                                     // window title, message boxes, start screen
#define GAME_FULL_NAME     "The Legend of Zelda: Breath of the Wild"
#define GAME_VERSION_LABEL "1.5.0"                                      // the only build the decomp's symbols fit
#define GAME_TITLE_ID      0x01007EF00011E000ull                        // program id (svcGetInfo 18)
// first 8 bytes of exefs/main's build id (NSO header 0x40): v1.5.0, the update inside the BotW cartridge image
#define GAME_BUILD_ID      {0x16, 0xa9, 0x19, 0x92, 0xbb, 0xa7, 0x12, 0x01}
#define GAME_EXE_NAME      "botwrecomp"                                 // + .exe; the import library is <name>.dll.a
#define GAME_DATA_DIR      "botwrecomp"                                 // Linux $XDG_DATA_HOME/<this>
#define GAME_BUNDLE_ID     "org.rewild.botwrecomp"                      // macOS Application Support/<this>
#define GAME_SETTINGS_FILE "botwrecomp.ini"
#define GAME_RELEASES_URL  "https://api.github.com/repos/J-isaac007/ReWild/releases?per_page=100"
#define GAME_RELEASE_PAGE  "https://github.com/J-isaac007/ReWild/releases/tag/"
