/* osrs/protocol.c - OSRS network protocol implementation
 * AUTO-GENERATED from rsprot source. Do not edit manually.
 * Revision: OSRS 241
 */

#include "osrs/protocol.h"
#include "osrs/rsa.h"
#include "osrs/xtea.h"
#include <stdio.h>
#include <string.h>

/* Server to Client (auto-generated from rsprot rev 241) */
const int8_t osrs_server_packet_sizes[256] = {
    4 /* MIDI_SONG_STOP */,
    5 /* CAM_TARGET_V3 */,
    OSRS_PKT_VAR_SHORT /* NPC_INFO_LARGE_V5 */,
    0 /* CAM_RESET */,
    9 /* NPC_HEADICON_SPECIFIC */,
    1 /* CHAT_FILTER_SETTINGS_PRIVATECHAT */,
    OSRS_PKT_VAR_BYTE /* SET_PLAYER_OP */,
    7 /* IF_OPENSUB */,
    OSRS_PKT_VAR_SHORT /* REFLECTION_CHECKER */,
    3 /* UPDATE_ZONE_PARTIAL_FOLLOWS */,
    2 /* LOC_DEL */,
    OSRS_PKT_VAR_BYTE /* VARCLAN */,
    6 /* VARP_LARGE */,
    14 /* OBJ_COUNT_SPECIFIC */,
    8 /* IF_SETROTATESPEED */,
    OSRS_PKT_VAR_SHORT /* MESSAGE_PRIVATE_ECHO */,
    6 /* IF_SETPLAYERMODEL_BASECOLOUR */,
    10 /* IF_SETANGLE */,
    OSRS_PKT_VAR_SHORT /* UPDATE_INV_PARTIAL */,
    OSRS_PKT_VAR_SHORT /* MESSAGE_PRIVATE */,
    5 /* NPC_ANIM_SPECIFIC */,
    1 /* HIDELOCOPS */,
    OSRS_PKT_VAR_SHORT /* UPDATE_INV_FULL */,
    4 /* IF_CLOSESUB */,
    OSRS_PKT_VAR_BYTE /* UNKNOWN_STRING */,
    OSRS_PKT_VAR_SHORT /* IF_RESYNC_V2 */,
    1 /* SET_HEATMAP_ENABLED */,
    OSRS_PKT_VAR_SHORT /* UPDATE_FRIENDCHAT_CHANNEL_FULL_V2 */,
    OSRS_PKT_VAR_SHORT /* PLAYER_INFO */,
    4 /* CAM_SMOOTHRESET */,
    6 /* LOC_ANIM_SPECIFIC */,
    2 /* UPDATE_RUNWEIGHT */,
    7 /* SOUND_AREA */,
    OSRS_PKT_VAR_SHORT /* LOC_ADD_CHANGE_V2 */,
    14 /* LOC_MERGE */,
    10 /* MIDI_SONG_V2 */,
    0 /* FRIENDLIST_LOADED */,
    OSRS_PKT_VAR_BYTE /* UPDATE_REBOOT_TIMER_V2 */,
    1 /* CAM_MODE */,
    OSRS_PKT_VAR_SHORT /* URL_OPEN */,
    7 /* CAM_ROTATETO */,
    10 /* OBJ_DEL_SPECIFIC */,
    OSRS_PKT_VAR_SHORT /* UPDATE_IGNORELIST */,
    1 /* MINIMAP_TOGGLE */,
    0 /* VARP_RESET */,
    2 /* UPDATE_INV_STOPTRANSMIT */,
    7 /* UPDATE_STAT_V2 */,
    3 /* SET_ACTIVE_WORLD_V2 */,
    27 /* PROJANIM_SPECIFIC_V4 */,
    OSRS_PKT_VAR_SHORT /* REBUILD_NORMAL_V2 */,
    6 /* HINT_ARROW */,
    7 /* CAM_ROTATEBY */,
    20 /* OBJ_CUSTOMISE_SPECIFIC */,
    20 /* UPDATE_STOCKMARKET_SLOT */,
    3 /* UPDATE_ZONE_FULL_FOLLOWS */,
    6 /* IF_SETCOLOUR */,
    OSRS_PKT_VAR_SHORT /* UPDATE_TRADINGPOST */,
    0 /* LOGOUT */,
    1 /* LOGOUT_WITHREASON */,
    6 /* IF_SETANIM */,
    0 /* VARCLAN_DISABLE */,
    OSRS_PKT_VAR_BYTE /* MESSAGE_FRIENDCHANNEL */,
    OSRS_PKT_VAR_BYTE /* MESSAGE_CLANCHANNEL_SYSTEM */,
    5 /* IF_SETHIDE */,
    2 /* UPDATE_RUNENERGY */,
    4 /* OCULUS_SYNC */,
    2 /* RESET_INTERACTION_MODE */,
    OSRS_PKT_VAR_BYTE /* SITE_SETTINGS */,
    8 /* ACCOUNT_FLAGS */,
    OSRS_PKT_VAR_SHORT /* CLANCHANNEL_FULL */,
    7 /* OBJ_ENABLED_OPS_SPECIFIC */,
    OSRS_PKT_VAR_SHORT /* CLANSETTINGS_DELTA */,
    9 /* PLAYER_SPOTANIM_SPECIFIC */,
    1 /* HIDEOBJOPS */,
    OSRS_PKT_VAR_BYTE /* MESSAGE_GAME */,
    1 /* HIDENPCOPS */,
    0 /* VARP_SYNC */,
    5 /* SYNTH_SOUND */,
    17 /* OBJ_ADD_SPECIFIC */,
    4 /* SET_INTERACTION_MODE */,
    OSRS_PKT_VAR_SHORT /* IF_SETTEXT */,
    OSRS_PKT_VAR_SHORT /* CLANSETTINGS_FULL */,
    6 /* IF_SETSCROLLPOS */,
    0 /* SERVER_TICK_END */,
    6 /* IF_SETNPCHEAD */,
    OSRS_PKT_VAR_SHORT /* NPC_INFO_SMALL_V5 */,
    6 /* MAP_ANIM */,
    5 /* CAM_TARGET_V4 */,
    0 /* VARCLAN_ENABLE */,
    6 /* IF_SETNPCHEAD_ACTIVE */,
    8 /* MAP_ANIM_SPECIFIC */,
    OSRS_PKT_VAR_BYTE /* LOGOUT_TRANSFER */,
    0 /* RESET_ANIMS */,
    2 /* PACKET_GROUP_START */,
    4 /* IF_CLEARINV */,
    OSRS_PKT_VAR_SHORT /* CLANCHANNEL_DELTA */,
    2 /* IF_OPENTOP */,
    3 /* VARP_SMALL */,
    8 /* IF_SETPLAYERMODEL_OBJ */,
    12 /* MIDI_SONG_WITHSECONDARY */,
    5 /* IF_SETPLAYERMODEL_BODYTYPE */,
    9 /* NPC_SPOTANIM_SPECIFIC */,
    8 /* IF_SETPOSITION */,
    4 /* CAM_SHAKE */,
    10 /* OBJ_UNCUSTOMISE_SPECIFIC */,
    OSRS_PKT_VAR_SHORT /* UPDATE_ZONE_PARTIAL_ENCLOSED */,
    10 /* IF_SETOBJECT */,
    8 /* MIDI_SWAP */,
    16 /* IF_SETEVENTS_V2 */,
    OSRS_PKT_VAR_SHORT /* REBUILD_WORLDENTITY_V4 */,
    4 /* LOC_ANIM */,
    OSRS_PKT_VAR_SHORT /* UPDATE_FRIENDLIST */,
    OSRS_PKT_VAR_BYTE /* UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER */,
    28 /* UPDATE_UID192 */,
    OSRS_PKT_VAR_SHORT /* RUNCLIENTSCRIPT */,
    8 /* SEND_PING */,
    2 /* SET_NPC_UPDATE_ORIGIN */,
    0 /* TRIGGER_ONDIALOGABORT */,
    5 /* MIDI_JINGLE */,
    3 /* ANIM_SPECIFIC */,
    4 /* IF_SETPLAYERHEAD */,
    5 /* IF_SETPLAYERMODEL_SELF */,
    OSRS_PKT_VAR_SHORT /* WORLDENTITY_INFO_V7 */,
    8 /* IF_MOVESUB */,
    2 /* CHAT_FILTER_SETTINGS */,
    OSRS_PKT_VAR_SHORT /* REBUILD_REGION_V2 */,
    OSRS_PKT_VAR_SHORT /* HISCORE_REPLY */,
    OSRS_PKT_VAR_BYTE /* MESSAGE_CLANCHANNEL */,
    15 /* CAM_MOVETO_ARC_V3 */,
    14 /* CAM_MOVETO_ARC_V2 */,
    11 /* CAM_MOVETO_CYCLES_V3 */,
    1 /* CAM_UNLOCK */,
    OSRS_PKT_VAR_SHORT /* GROUP_FULL */,
    13 /* GROUP_VAR_LONG */,
    10 /* CAM_MOVETO_CYCLES_V2 */,
    4 /* CAM_SKYBOX */,
    OSRS_PKT_VAR_SHORT /* GROUP_VAR */,
    10 /* CAM_LOOKAT_CYCLES */,
    1 /* AMBIENTSOUND_STOP */,
    8 /* CAM_LOOKAT_V2 */,
    9 /* GROUP_VAR_INT */,
    9 /* CAM_ROTATETO_COORDINATE_V2 */,
    8 /* IF_SETMODEL_V2 */,
    4 /* SET_MAP_FLAG_V2 */,
    9 /* CAM_LOOKAT_V3 */,
    3 /* AMBIENTSOUND_START */,
    8 /* CAM_MOVETO_V2 */,
    9 /* CAM_MOVETO_V3 */,
    11 /* CAM_ROTATETO_COORDINATE_V3 */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
};

/* Client to Server (auto-generated from rsprot rev 241) */
const int8_t osrs_client_packet_sizes[256] = {
    2 /* OPNPC6 */,
    10 /* SEND_PING_REPLY */,
    1 /* CLANSETTINGS_FULL_REQUEST */,
    3 /* OPPLAYER2 */,
    0 /* IDLE */,
    0 /* EXIT_FREECAM */,
    OSRS_PKT_VAR_BYTE /* EVENT_NATIVE_MOUSE_MOVE */,
    8 /* OPOBJ3_V2 */,
    4 /* EVENT_CAMERA_POSITION */,
    OSRS_PKT_VAR_BYTE /* IGNORELIST_DEL */,
    5 /* WINDOW_STATUS */,
    -1 /* ??? */,
    OSRS_PKT_VAR_BYTE /* REFLECTION_CHECK_REPLY */,
    4 /* OPNPC2_V2 */,
    3 /* OPWORLDENTITY1 */,
    8 /* OPLOC4_V2 */,
    2 /* EVENT_MOUSE_SCROLL */,
    OSRS_PKT_VAR_BYTE /* HISCORE_REQUEST */,
    -1 /* ??? */,
    4 /* OPNPC3_V2 */,
    OSRS_PKT_VAR_BYTE /* FRIENDCHAT_JOIN_LEAVE */,
    -1 /* ??? */,
    3 /* OPPLAYER7 */,
    11 /* OPNPCU */,
    0 /* MAP_BUILD_COMPLETE */,
    -1 /* ??? */,
    OSRS_PKT_VAR_BYTE /* RESUME_P_NAMEDIALOG */,
    16 /* IF_BUTTONT */,
    OSRS_PKT_VAR_BYTE /* FRIENDLIST_ADD */,
    1 /* EVENT_APPLET_FOCUS */,
    -1 /* ??? */,
    8 /* OPOBJ4_V2 */,
    2 /* RESUME_P_OBJDIALOG */,
    4 /* DETECT_MODIFIED_CLIENT */,
    OSRS_PKT_VAR_BYTE /* CLIENT_CHEAT */,
    7 /* EVENT_MOUSE_CLICK_V2 */,
    -1 /* ??? */,
    3 /* OPPLAYER4 */,
    OSRS_PKT_VAR_BYTE /* CLANCHANNEL_KICKUSER */,
    4 /* SOUND_JINGLEEND */,
    10 /* IF_SUBOP */,
    -1 /* ??? */,
    -1 /* ??? */,
    8 /* OPOBJ5_V2 */,
    1 /* SET_HEADING */,
    4 /* OPNPC4_V2 */,
    8 /* OPOBJ1_V2 */,
    9 /* IF_BUTTONX */,
    16 /* IF_BUTTOND */,
    -1 /* ??? */,
    9 /* TELEPORT */,
    4 /* IF_BUTTON */,
    15 /* OPLOCU */,
    3 /* OPPLAYER8 */,
    OSRS_PKT_VAR_SHORT /* EVENT_KEYBOARD */,
    OSRS_PKT_VAR_BYTE /* FRIENDCHAT_SETRANK */,
    OSRS_PKT_VAR_SHORT /* IF_SCRIPT_TRIGGER */,
    3 /* OPPLAYER5 */,
    6 /* OPOBJ6 */,
    3 /* SET_CHATFILTERSETTINGS */,
    OSRS_PKT_VAR_BYTE /* SEND_SNAPSHOT */,
    2 /* MEMBERSHIP_PROMOTION_ELIGIBILITY */,
    15 /* OPLOCT */,
    8 /* RESUME_P_COUNTDIALOG_LONG */,
    OSRS_PKT_VAR_BYTE /* RESUME_P_STRINGDIALOG */,
    OSRS_PKT_VAR_BYTE /* FRIENDLIST_DEL */,
    6 /* EVENT_MOUSE_CLICK_V1 */,
    OSRS_PKT_VAR_BYTE /* MOVE_MINIMAPCLICK */,
    -1 /* ??? */,
    OSRS_PKT_VAR_BYTE /* MESSAGE_PUBLIC */,
    3 /* OPPLAYER6 */,
    -1 /* ??? */,
    11 /* OPWORLDENTITYT */,
    8 /* OPLOC1_V2 */,
    8 /* OPOBJ2_V2 */,
    4 /* RESUME_P_COUNTDIALOG */,
    15 /* OPOBJT */,
    26 /* UPDATE_PLAYER_MODEL_V2 */,
    2 /* OPLOC6 */,
    11 /* OPPLAYERT */,
    3 /* OPWORLDENTITY2 */,
    -1 /* ??? */,
    OSRS_PKT_VAR_BYTE /* IGNORELIST_ADD */,
    3 /* OPWORLDENTITY4 */,
    -1 /* ??? */,
    4 /* OPNPC5_V2 */,
    11 /* OPNPCT */,
    11 /* OPPLAYERU */,
    -1 /* ??? */,
    0 /* NO_TIMEOUT */,
    11 /* OPWORLDENTITYU */,
    22 /* IF_CRMVIEW_OP */,
    3 /* OPPLAYER3 */,
    3 /* OPPLAYER1 */,
    -1 /* ??? */,
    0 /* CLOSE_MODAL */,
    OSRS_PKT_VAR_BYTE /* AFFINEDCLANSETTINGS_SETMUTED_FROMCHANNEL */,
    OSRS_PKT_VAR_BYTE /* CONNECTION_TELEMETRY */,
    2 /* OPWORLDENTITY6 */,
    4 /* CLICKWORLDMAP */,
    3 /* OPWORLDENTITY3 */,
    OSRS_PKT_VAR_BYTE /* FRIENDCHAT_KICK */,
    4 /* OPNPC1_V2 */,
    8 /* OPLOC2_V2 */,
    8 /* OPLOC3_V2 */,
    1 /* RSEVEN_STATUS */,
    3 /* OPWORLDENTITY5 */,
    -1 /* ??? */,
    OSRS_PKT_VAR_SHORT /* BUG_REPORT */,
    -1 /* ??? */,
    OSRS_PKT_VAR_BYTE /* EVENT_MOUSE_MOVE */,
    OSRS_PKT_VAR_SHORT /* MESSAGE_PRIVATE */,
    1 /* CLANCHANNEL_FULL_REQUEST */,
    15 /* OPOBJU */,
    OSRS_PKT_VAR_BYTE /* MOVE_GAMECLICK */,
    6 /* RESUME_PAUSEBUTTON */,
    8 /* OPLOC5_V2 */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
    -1 /* ??? */,
};

const char *osrs_server_packet_name(int opcode) {
  static const char *names[256] = {
      [0] = "MIDI_SONG_STOP",
      [1] = "CAM_TARGET_V3",
      [2] = "NPC_INFO_LARGE_V5",
      [3] = "CAM_RESET",
      [4] = "NPC_HEADICON_SPECIFIC",
      [5] = "CHAT_FILTER_SETTINGS_PRIVATECHAT",
      [6] = "SET_PLAYER_OP",
      [7] = "IF_OPENSUB",
      [8] = "REFLECTION_CHECKER",
      [9] = "UPDATE_ZONE_PARTIAL_FOLLOWS",
      [10] = "LOC_DEL",
      [11] = "VARCLAN",
      [12] = "VARP_LARGE",
      [13] = "OBJ_COUNT_SPECIFIC",
      [14] = "IF_SETROTATESPEED",
      [15] = "MESSAGE_PRIVATE_ECHO",
      [16] = "IF_SETPLAYERMODEL_BASECOLOUR",
      [17] = "IF_SETANGLE",
      [18] = "UPDATE_INV_PARTIAL",
      [19] = "MESSAGE_PRIVATE",
      [20] = "NPC_ANIM_SPECIFIC",
      [21] = "HIDELOCOPS",
      [22] = "UPDATE_INV_FULL",
      [23] = "IF_CLOSESUB",
      [24] = "UNKNOWN_STRING",
      [25] = "IF_RESYNC_V2",
      [26] = "SET_HEATMAP_ENABLED",
      [27] = "UPDATE_FRIENDCHAT_CHANNEL_FULL_V2",
      [28] = "PLAYER_INFO",
      [29] = "CAM_SMOOTHRESET",
      [30] = "LOC_ANIM_SPECIFIC",
      [31] = "UPDATE_RUNWEIGHT",
      [32] = "SOUND_AREA",
      [33] = "LOC_ADD_CHANGE_V2",
      [34] = "LOC_MERGE",
      [35] = "MIDI_SONG_V2",
      [36] = "FRIENDLIST_LOADED",
      [37] = "UPDATE_REBOOT_TIMER_V2",
      [38] = "CAM_MODE",
      [39] = "URL_OPEN",
      [40] = "CAM_ROTATETO",
      [41] = "OBJ_DEL_SPECIFIC",
      [42] = "UPDATE_IGNORELIST",
      [43] = "MINIMAP_TOGGLE",
      [44] = "VARP_RESET",
      [45] = "UPDATE_INV_STOPTRANSMIT",
      [46] = "UPDATE_STAT_V2",
      [47] = "SET_ACTIVE_WORLD_V2",
      [48] = "PROJANIM_SPECIFIC_V4",
      [49] = "REBUILD_NORMAL_V2",
      [50] = "HINT_ARROW",
      [51] = "CAM_ROTATEBY",
      [52] = "OBJ_CUSTOMISE_SPECIFIC",
      [53] = "UPDATE_STOCKMARKET_SLOT",
      [54] = "UPDATE_ZONE_FULL_FOLLOWS",
      [55] = "IF_SETCOLOUR",
      [56] = "UPDATE_TRADINGPOST",
      [57] = "LOGOUT",
      [58] = "LOGOUT_WITHREASON",
      [59] = "IF_SETANIM",
      [60] = "VARCLAN_DISABLE",
      [61] = "MESSAGE_FRIENDCHANNEL",
      [62] = "MESSAGE_CLANCHANNEL_SYSTEM",
      [63] = "IF_SETHIDE",
      [64] = "UPDATE_RUNENERGY",
      [65] = "OCULUS_SYNC",
      [66] = "RESET_INTERACTION_MODE",
      [67] = "SITE_SETTINGS",
      [68] = "ACCOUNT_FLAGS",
      [69] = "CLANCHANNEL_FULL",
      [70] = "OBJ_ENABLED_OPS_SPECIFIC",
      [71] = "CLANSETTINGS_DELTA",
      [72] = "PLAYER_SPOTANIM_SPECIFIC",
      [73] = "HIDEOBJOPS",
      [74] = "MESSAGE_GAME",
      [75] = "HIDENPCOPS",
      [76] = "VARP_SYNC",
      [77] = "SYNTH_SOUND",
      [78] = "OBJ_ADD_SPECIFIC",
      [79] = "SET_INTERACTION_MODE",
      [80] = "IF_SETTEXT",
      [81] = "CLANSETTINGS_FULL",
      [82] = "IF_SETSCROLLPOS",
      [83] = "SERVER_TICK_END",
      [84] = "IF_SETNPCHEAD",
      [85] = "NPC_INFO_SMALL_V5",
      [86] = "MAP_ANIM",
      [87] = "CAM_TARGET_V4",
      [88] = "VARCLAN_ENABLE",
      [89] = "IF_SETNPCHEAD_ACTIVE",
      [90] = "MAP_ANIM_SPECIFIC",
      [91] = "LOGOUT_TRANSFER",
      [92] = "RESET_ANIMS",
      [93] = "PACKET_GROUP_START",
      [94] = "IF_CLEARINV",
      [95] = "CLANCHANNEL_DELTA",
      [96] = "IF_OPENTOP",
      [97] = "VARP_SMALL",
      [98] = "IF_SETPLAYERMODEL_OBJ",
      [99] = "MIDI_SONG_WITHSECONDARY",
      [100] = "IF_SETPLAYERMODEL_BODYTYPE",
      [101] = "NPC_SPOTANIM_SPECIFIC",
      [102] = "IF_SETPOSITION",
      [103] = "CAM_SHAKE",
      [104] = "OBJ_UNCUSTOMISE_SPECIFIC",
      [105] = "UPDATE_ZONE_PARTIAL_ENCLOSED",
      [106] = "IF_SETOBJECT",
      [107] = "MIDI_SWAP",
      [108] = "IF_SETEVENTS_V2",
      [109] = "REBUILD_WORLDENTITY_V4",
      [110] = "LOC_ANIM",
      [111] = "UPDATE_FRIENDLIST",
      [112] = "UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER",
      [113] = "UPDATE_UID192",
      [114] = "RUNCLIENTSCRIPT",
      [115] = "SEND_PING",
      [116] = "SET_NPC_UPDATE_ORIGIN",
      [117] = "TRIGGER_ONDIALOGABORT",
      [118] = "MIDI_JINGLE",
      [119] = "ANIM_SPECIFIC",
      [120] = "IF_SETPLAYERHEAD",
      [121] = "IF_SETPLAYERMODEL_SELF",
      [122] = "WORLDENTITY_INFO_V7",
      [123] = "IF_MOVESUB",
      [124] = "CHAT_FILTER_SETTINGS",
      [125] = "REBUILD_REGION_V2",
      [126] = "HISCORE_REPLY",
      [127] = "MESSAGE_CLANCHANNEL",
      [128] = "CAM_MOVETO_ARC_V3",
      [129] = "CAM_MOVETO_ARC_V2",
      [130] = "CAM_MOVETO_CYCLES_V3",
      [131] = "CAM_UNLOCK",
      [132] = "GROUP_FULL",
      [133] = "GROUP_VAR_LONG",
      [134] = "CAM_MOVETO_CYCLES_V2",
      [135] = "CAM_SKYBOX",
      [136] = "GROUP_VAR",
      [137] = "CAM_LOOKAT_CYCLES",
      [138] = "AMBIENTSOUND_STOP",
      [139] = "CAM_LOOKAT_V2",
      [140] = "GROUP_VAR_INT",
      [141] = "CAM_ROTATETO_COORDINATE_V2",
      [142] = "IF_SETMODEL_V2",
      [143] = "SET_MAP_FLAG_V2",
      [144] = "CAM_LOOKAT_V3",
      [145] = "AMBIENTSOUND_START",
      [146] = "CAM_MOVETO_V2",
      [147] = "CAM_MOVETO_V3",
      [148] = "CAM_ROTATETO_COORDINATE_V3",
      [149] = NULL,
      [150] = NULL,
      [151] = NULL,
      [152] = NULL,
      [153] = NULL,
      [154] = NULL,
      [155] = NULL,
      [156] = NULL,
      [157] = NULL,
      [158] = NULL,
      [159] = NULL,
      [160] = NULL,
      [161] = NULL,
      [162] = NULL,
      [163] = NULL,
      [164] = NULL,
      [165] = NULL,
      [166] = NULL,
      [167] = NULL,
      [168] = NULL,
      [169] = NULL,
      [170] = NULL,
      [171] = NULL,
      [172] = NULL,
      [173] = NULL,
      [174] = NULL,
      [175] = NULL,
      [176] = NULL,
      [177] = NULL,
      [178] = NULL,
      [179] = NULL,
      [180] = NULL,
      [181] = NULL,
      [182] = NULL,
      [183] = NULL,
      [184] = NULL,
      [185] = NULL,
      [186] = NULL,
      [187] = NULL,
      [188] = NULL,
      [189] = NULL,
      [190] = NULL,
      [191] = NULL,
      [192] = NULL,
      [193] = NULL,
      [194] = NULL,
      [195] = NULL,
      [196] = NULL,
      [197] = NULL,
      [198] = NULL,
      [199] = NULL,
      [200] = NULL,
      [201] = NULL,
      [202] = NULL,
      [203] = NULL,
      [204] = NULL,
      [205] = NULL,
      [206] = NULL,
      [207] = NULL,
      [208] = NULL,
      [209] = NULL,
      [210] = NULL,
      [211] = NULL,
      [212] = NULL,
      [213] = NULL,
      [214] = NULL,
      [215] = NULL,
      [216] = NULL,
      [217] = NULL,
      [218] = NULL,
      [219] = NULL,
      [220] = NULL,
      [221] = NULL,
      [222] = NULL,
      [223] = NULL,
      [224] = NULL,
      [225] = NULL,
      [226] = NULL,
      [227] = NULL,
      [228] = NULL,
      [229] = NULL,
      [230] = NULL,
      [231] = NULL,
      [232] = NULL,
      [233] = NULL,
      [234] = NULL,
      [235] = NULL,
      [236] = NULL,
      [237] = NULL,
      [238] = NULL,
      [239] = NULL,
      [240] = NULL,
      [241] = NULL,
      [242] = NULL,
      [243] = NULL,
      [244] = NULL,
      [245] = NULL,
      [246] = NULL,
      [247] = NULL,
      [248] = NULL,
      [249] = NULL,
      [250] = NULL,
      [251] = NULL,
      [252] = NULL,
      [253] = NULL,
      [254] = NULL,
      [255] = NULL,
  };
  if (opcode >= 0 && opcode < 256 && names[opcode])
    return names[opcode];
  return "UNKNOWN";
}

const char *osrs_client_packet_name(int opcode) {
  static const char *names[256] = {
      [0] = "OPNPC6",
      [1] = "SEND_PING_REPLY",
      [2] = "CLANSETTINGS_FULL_REQUEST",
      [3] = "OPPLAYER2",
      [4] = "IDLE",
      [5] = "EXIT_FREECAM",
      [6] = "EVENT_NATIVE_MOUSE_MOVE",
      [7] = "OPOBJ3_V2",
      [8] = "EVENT_CAMERA_POSITION",
      [9] = "IGNORELIST_DEL",
      [10] = "WINDOW_STATUS",
      [11] = NULL,
      [12] = "REFLECTION_CHECK_REPLY",
      [13] = "OPNPC2_V2",
      [14] = "OPWORLDENTITY1",
      [15] = "OPLOC4_V2",
      [16] = "EVENT_MOUSE_SCROLL",
      [17] = "HISCORE_REQUEST",
      [18] = NULL,
      [19] = "OPNPC3_V2",
      [20] = "FRIENDCHAT_JOIN_LEAVE",
      [21] = NULL,
      [22] = "OPPLAYER7",
      [23] = "OPNPCU",
      [24] = "MAP_BUILD_COMPLETE",
      [25] = NULL,
      [26] = "RESUME_P_NAMEDIALOG",
      [27] = "IF_BUTTONT",
      [28] = "FRIENDLIST_ADD",
      [29] = "EVENT_APPLET_FOCUS",
      [30] = NULL,
      [31] = "OPOBJ4_V2",
      [32] = "RESUME_P_OBJDIALOG",
      [33] = "DETECT_MODIFIED_CLIENT",
      [34] = "CLIENT_CHEAT",
      [35] = "EVENT_MOUSE_CLICK_V2",
      [36] = NULL,
      [37] = "OPPLAYER4",
      [38] = "CLANCHANNEL_KICKUSER",
      [39] = "SOUND_JINGLEEND",
      [40] = "IF_SUBOP",
      [41] = NULL,
      [42] = NULL,
      [43] = "OPOBJ5_V2",
      [44] = "SET_HEADING",
      [45] = "OPNPC4_V2",
      [46] = "OPOBJ1_V2",
      [47] = "IF_BUTTONX",
      [48] = "IF_BUTTOND",
      [49] = NULL,
      [50] = "TELEPORT",
      [51] = "IF_BUTTON",
      [52] = "OPLOCU",
      [53] = "OPPLAYER8",
      [54] = "EVENT_KEYBOARD",
      [55] = "FRIENDCHAT_SETRANK",
      [56] = "IF_SCRIPT_TRIGGER",
      [57] = "OPPLAYER5",
      [58] = "OPOBJ6",
      [59] = "SET_CHATFILTERSETTINGS",
      [60] = "SEND_SNAPSHOT",
      [61] = "MEMBERSHIP_PROMOTION_ELIGIBILITY",
      [62] = "OPLOCT",
      [63] = "RESUME_P_COUNTDIALOG_LONG",
      [64] = "RESUME_P_STRINGDIALOG",
      [65] = "FRIENDLIST_DEL",
      [66] = "EVENT_MOUSE_CLICK_V1",
      [67] = "MOVE_MINIMAPCLICK",
      [68] = NULL,
      [69] = "MESSAGE_PUBLIC",
      [70] = "OPPLAYER6",
      [71] = NULL,
      [72] = "OPWORLDENTITYT",
      [73] = "OPLOC1_V2",
      [74] = "OPOBJ2_V2",
      [75] = "RESUME_P_COUNTDIALOG",
      [76] = "OPOBJT",
      [77] = "UPDATE_PLAYER_MODEL_V2",
      [78] = "OPLOC6",
      [79] = "OPPLAYERT",
      [80] = "OPWORLDENTITY2",
      [81] = NULL,
      [82] = "IGNORELIST_ADD",
      [83] = "OPWORLDENTITY4",
      [84] = NULL,
      [85] = "OPNPC5_V2",
      [86] = "OPNPCT",
      [87] = "OPPLAYERU",
      [88] = NULL,
      [89] = "NO_TIMEOUT",
      [90] = "OPWORLDENTITYU",
      [91] = "IF_CRMVIEW_OP",
      [92] = "OPPLAYER3",
      [93] = "OPPLAYER1",
      [94] = NULL,
      [95] = "CLOSE_MODAL",
      [96] = "AFFINEDCLANSETTINGS_SETMUTED_FROMCHANNEL",
      [97] = "CONNECTION_TELEMETRY",
      [98] = "OPWORLDENTITY6",
      [99] = "CLICKWORLDMAP",
      [100] = "OPWORLDENTITY3",
      [101] = "FRIENDCHAT_KICK",
      [102] = "OPNPC1_V2",
      [103] = "OPLOC2_V2",
      [104] = "OPLOC3_V2",
      [105] = "RSEVEN_STATUS",
      [106] = "OPWORLDENTITY5",
      [107] = NULL,
      [108] = "BUG_REPORT",
      [109] = NULL,
      [110] = "EVENT_MOUSE_MOVE",
      [111] = "MESSAGE_PRIVATE",
      [112] = "CLANCHANNEL_FULL_REQUEST",
      [113] = "OPOBJU",
      [114] = "MOVE_GAMECLICK",
      [115] = "RESUME_PAUSEBUTTON",
      [116] = "OPLOC5_V2",
      [117] = NULL,
      [118] = NULL,
      [119] = NULL,
      [120] = NULL,
      [121] = NULL,
      [122] = NULL,
      [123] = NULL,
      [124] = NULL,
      [125] = NULL,
      [126] = NULL,
      [127] = NULL,
      [128] = NULL,
      [129] = NULL,
      [130] = NULL,
      [131] = NULL,
      [132] = NULL,
      [133] = NULL,
      [134] = NULL,
      [135] = NULL,
      [136] = NULL,
      [137] = NULL,
      [138] = NULL,
      [139] = NULL,
      [140] = NULL,
      [141] = NULL,
      [142] = NULL,
      [143] = NULL,
      [144] = NULL,
      [145] = NULL,
      [146] = NULL,
      [147] = NULL,
      [148] = NULL,
      [149] = NULL,
      [150] = NULL,
      [151] = NULL,
      [152] = NULL,
      [153] = NULL,
      [154] = NULL,
      [155] = NULL,
      [156] = NULL,
      [157] = NULL,
      [158] = NULL,
      [159] = NULL,
      [160] = NULL,
      [161] = NULL,
      [162] = NULL,
      [163] = NULL,
      [164] = NULL,
      [165] = NULL,
      [166] = NULL,
      [167] = NULL,
      [168] = NULL,
      [169] = NULL,
      [170] = NULL,
      [171] = NULL,
      [172] = NULL,
      [173] = NULL,
      [174] = NULL,
      [175] = NULL,
      [176] = NULL,
      [177] = NULL,
      [178] = NULL,
      [179] = NULL,
      [180] = NULL,
      [181] = NULL,
      [182] = NULL,
      [183] = NULL,
      [184] = NULL,
      [185] = NULL,
      [186] = NULL,
      [187] = NULL,
      [188] = NULL,
      [189] = NULL,
      [190] = NULL,
      [191] = NULL,
      [192] = NULL,
      [193] = NULL,
      [194] = NULL,
      [195] = NULL,
      [196] = NULL,
      [197] = NULL,
      [198] = NULL,
      [199] = NULL,
      [200] = NULL,
      [201] = NULL,
      [202] = NULL,
      [203] = NULL,
      [204] = NULL,
      [205] = NULL,
      [206] = NULL,
      [207] = NULL,
      [208] = NULL,
      [209] = NULL,
      [210] = NULL,
      [211] = NULL,
      [212] = NULL,
      [213] = NULL,
      [214] = NULL,
      [215] = NULL,
      [216] = NULL,
      [217] = NULL,
      [218] = NULL,
      [219] = NULL,
      [220] = NULL,
      [221] = NULL,
      [222] = NULL,
      [223] = NULL,
      [224] = NULL,
      [225] = NULL,
      [226] = NULL,
      [227] = NULL,
      [228] = NULL,
      [229] = NULL,
      [230] = NULL,
      [231] = NULL,
      [232] = NULL,
      [233] = NULL,
      [234] = NULL,
      [235] = NULL,
      [236] = NULL,
      [237] = NULL,
      [238] = NULL,
      [239] = NULL,
      [240] = NULL,
      [241] = NULL,
      [242] = NULL,
      [243] = NULL,
      [244] = NULL,
      [245] = NULL,
      [246] = NULL,
      [247] = NULL,
      [248] = NULL,
      [249] = NULL,
      [250] = NULL,
      [251] = NULL,
      [252] = NULL,
      [253] = NULL,
      [254] = NULL,
      [255] = NULL,
  };
  if (opcode >= 0 && opcode < 256 && names[opcode])
    return names[opcode];
  return "UNKNOWN";
}

void osrs_protocol_init(osrs_protocol_t *proto) {
  memset(proto, 0, sizeof(*proto));
  proto->state = OSRS_CONN_IDLE;
}

void osrs_protocol_set_game_keys(osrs_protocol_t *proto,
                                 const uint32_t keys[4]) {
  osrs_isaac_init(&proto->isaac_in, keys);
  osrs_isaac_init(&proto->isaac_out, keys);
}

int osrs_game_read_packet(osrs_protocol_t *proto, const uint8_t *data,
                          size_t len, osrs_packet_t *packet, size_t *consumed) {
  *consumed = 0;
  if (len < 1)
    return -1;

  uint32_t isaac1 = osrs_isaac_peek(&proto->isaac_in) & 0xFF;
  int var1 = (data[0] - (int)isaac1) & 0xFF;
  int opcode;
  size_t pos;

  if (var1 < 128) {
    opcode = var1;
    pos = 1;
  } else {
    if (len < 2)
      return -1;
    uint32_t isaac2 = osrs_isaac_peek2(&proto->isaac_in) & 0xFF;
    int var2 = (data[1] - (int)isaac2) & 0xFF;
    opcode = ((var1 - 128) << 8) + var2;
    pos = 2;
  }

  int size = osrs_server_packet_sizes[opcode & 0xFF];

  if (size == OSRS_PKT_VAR_BYTE) {
    if (len < pos + 1)
      return -1;
    size = data[pos];
    pos += 1;
  } else if (size == OSRS_PKT_VAR_SHORT) {
    if (len < pos + 2)
      return -1;
    size = ((int)data[pos] << 8) | data[pos + 1];
    pos += 2;
  } else if (size < 0) {
    return -2; /* unknown opcode */
  }

  size_t total = pos + (size_t)size;
  if (len < total)
    return -1;

  /* Full packet available -- commit ISAAC consumption */
  (void)osrs_isaac_next(&proto->isaac_in);
  if (var1 >= 128)
    (void)osrs_isaac_next(&proto->isaac_in);

  packet->len = (size_t)size;
  if (packet->len > sizeof(packet->data))
    return -2;
  memcpy(packet->data, data + pos, packet->len);
  *consumed = total;
  return opcode;
}

int osrs_game_write_packet(osrs_protocol_t *proto, int opcode,
                           const uint8_t *payload, size_t payload_len,
                           uint8_t *out, size_t out_len) {
  int8_t size_type =
      (opcode >= 0 && opcode < 256) ? osrs_client_packet_sizes[opcode] : -9;

  size_t header_len = (opcode < 128) ? 1 : 2;
  size_t len_prefix = 0;
  if (size_type == OSRS_PKT_VAR_BYTE)
    len_prefix = 1;
  else if (size_type == OSRS_PKT_VAR_SHORT)
    len_prefix = 2;

  if (out_len < header_len + len_prefix + payload_len)
    return -1;

  size_t pos = 0;
  uint32_t isaac_val = osrs_isaac_next(&proto->isaac_out);

  if (opcode < 128) {
    out[pos++] = (uint8_t)(opcode + isaac_val);
  } else {
    out[pos++] = (uint8_t)(((opcode >> 8) + 128) + isaac_val);
    uint32_t isaac_val2 = osrs_isaac_next(&proto->isaac_out);
    out[pos++] = (uint8_t)((opcode & 0xFF) + isaac_val2);
  }

  if (size_type == OSRS_PKT_VAR_BYTE) {
    out[pos++] = (uint8_t)payload_len;
  } else if (size_type == OSRS_PKT_VAR_SHORT) {
    out[pos++] = (uint8_t)(payload_len >> 8);
    out[pos++] = (uint8_t)payload_len;
  }

  memcpy(out + pos, payload, payload_len);
  return (int)(pos + payload_len);
}

static void osrs_packet_init(osrs_packet_t *p) { p->len = 0; }

static void osrs_p1(osrs_packet_t *p, uint8_t v) {
  if (p->len < sizeof(p->data))
    p->data[p->len++] = v;
}

static void osrs_p2(osrs_packet_t *p, uint16_t v) {
  osrs_p1(p, (uint8_t)(v >> 8));
  osrs_p1(p, (uint8_t)v);
}

static void osrs_p2alt1(osrs_packet_t *p, uint16_t v) {
  osrs_p1(p, (uint8_t)(v >> 8));
  osrs_p1(p, (uint8_t)(v + 128));
}

static void osrs_p1alt3(osrs_packet_t *p, uint8_t v) {
  osrs_p1(p, (uint8_t)(v + 128));
}

size_t osrs_build_move_gameclick(osrs_packet_t *p, int x, int z, bool run) {
  osrs_packet_init(p);
  osrs_p2alt1(p, (uint16_t)x);
  osrs_p2alt1(p, (uint16_t)z);
  osrs_p1alt3(p, run ? 1 : 0);
  return p->len;
}

size_t osrs_build_window_status(osrs_packet_t *p, int mode, int w, int h) {
  osrs_packet_init(p);
  osrs_p1(p, (uint8_t)mode);
  osrs_p2(p, (uint16_t)w);
  osrs_p2(p, (uint16_t)h);
  return p->len;
}

size_t osrs_build_event_applet_focus(osrs_packet_t *p, bool focused) {
  osrs_packet_init(p);
  osrs_p1(p, focused ? 1 : 0);
  return p->len;
}

/* -------------------------------------------------------------------------- */
/* Login handshake                                                            */
/* -------------------------------------------------------------------------- */

size_t osrs_login_build_init(uint8_t *out) {
  out[0] = 14;
  return 1;
}

int osrs_login_process_init_response(osrs_protocol_t *proto,
                                     const uint8_t *data, size_t len,
                                     size_t *consumed) {
  *consumed = 0;
  if (len < 1)
    return 0;
  uint8_t code = data[0];
  if (code != 0) {
    *consumed = 1;
    return -code;
  }
  if (len < 9)
    return 0;
  proto->session_id = ((uint64_t)data[1] << 56) | ((uint64_t)data[2] << 48) |
                      ((uint64_t)data[3] << 40) | ((uint64_t)data[4] << 32) |
                      ((uint64_t)data[5] << 24) | ((uint64_t)data[6] << 16) |
                      ((uint64_t)data[7] << 8) | (uint64_t)data[8];
  fprintf(stderr, "[DEBUG] init session_id=0x%016llx\n",
          (unsigned long long)proto->session_id);
  *consumed = 9;
  return 1;
}

static void lp1(uint8_t **p, uint8_t v) {
  **p = v;
  (*p)++;
}

static void lp2(uint8_t **p, uint16_t v) {
  lp1(p, (uint8_t)(v >> 8));
  lp1(p, (uint8_t)v);
}

static void lp4(uint8_t **p, uint32_t v) {
  lp2(p, (uint16_t)(v >> 16));
  lp2(p, (uint16_t)v);
}

static void lp8(uint8_t **p, uint64_t v) {
  lp4(p, (uint32_t)(v >> 32));
  lp4(p, (uint32_t)v);
}

static void ljstr(uint8_t **p, const char *s) {
  size_t n = strlen(s);
  memcpy(*p, s, n);
  *p += n;
  lp1(p, 0);
}

static void ljstr2(uint8_t **p, const char *s) {
  lp1(p, 0); /* version/type flag */
  ljstr(p, s);
}

static void lpu24(uint8_t **p, uint32_t v) {
  lp1(p, (uint8_t)(v >> 16));
  lp2(p, (uint16_t)v);
}

/* 4-byte alt encodings (must match rsprot decoder exactly) */
static void lp4_default(uint8_t **p, uint32_t v) {
  (*p)[0] = (uint8_t)(v >> 24);
  (*p)[1] = (uint8_t)(v >> 16);
  (*p)[2] = (uint8_t)(v >> 8);
  (*p)[3] = (uint8_t)v;
  *p += 4;
}

static void lp4_alt1(uint8_t **p, uint32_t v) {
  (*p)[0] = (uint8_t)v;
  (*p)[1] = (uint8_t)(v >> 8);
  (*p)[2] = (uint8_t)(v >> 16);
  (*p)[3] = (uint8_t)(v >> 24);
  *p += 4;
}

static void lp4_alt2(uint8_t **p, uint32_t v) {
  (*p)[0] = (uint8_t)(v >> 8);
  (*p)[1] = (uint8_t)v;
  (*p)[2] = (uint8_t)(v >> 24);
  (*p)[3] = (uint8_t)(v >> 16);
  *p += 4;
}

static void lp4_alt3(uint8_t **p, uint32_t v) {
  (*p)[0] = (uint8_t)(v >> 16);
  (*p)[1] = (uint8_t)(v >> 24);
  (*p)[2] = (uint8_t)v;
  (*p)[3] = (uint8_t)(v >> 8);
  *p += 4;
}

static void write_platform_stats(uint8_t **pp) {
  uint8_t *p = *pp;
  lp1(&p, 9);       /* struct version */
  lp1(&p, 3);       /* osType = Linux */
  lp1(&p, 1);       /* os64Bit */
  lp2(&p, 61840);   /* osVersion */
  lp1(&p, 1);       /* javaVendor */
  lp1(&p, 26);      /* javaMajor */
  lp1(&p, 0);       /* javaMinor */
  lp1(&p, 2);       /* javaPatch */
  lp1(&p, 1);       /* notApplet */
  lp2(&p, 4096);    /* maxMemoryMB */
  lp1(&p, 24);      /* processors */
  lpu24(&p, 98855); /* systemMemoryMB */
  lp2(&p, 3330);    /* systemSpeedMHz */
  ljstr2(&p, "");   /* gpuDxName */
  ljstr2(&p, "AMD Radeon RX Vega");
  ljstr2(&p, ""); /* gpuDxVersion */
  ljstr2(&p, "4.6 (Compatibility Profile) Mesa 26.1.5");
  lp1(&p, 1);    /* driverMonth */
  lp2(&p, 2026); /* driverYear */
  ljstr2(&p, "GenuineIntel");
  ljstr2(&p, "Intel(R) Xeon(R) CPU X5680 @ 3.33GHz");
  lp1(&p, 12);         /* cpuCount1 (cores) */
  lp1(&p, 24);         /* cpuCount2 (threads) */
  lp4(&p, 0xbfebfbff); /* cpuFeature1 */
  lp4(&p, 0x00000000); /* cpuFeature2 */
  lp4(&p, 0x00000000); /* cpuFeature3 */
  lp4(&p, 0x000206c2); /* cpuSignature */
  ljstr2(&p, "osrs-launcher");
  ljstr2(&p, "forge.ordl.org");
  *pp = p;
}

size_t osrs_login_build_gamelogin(osrs_protocol_t *proto, uint8_t *out) {
  /* --- Build RSA plaintext --- */
  uint8_t rsa_plain[128];
  memset(rsa_plain, 0, sizeof(rsa_plain));
  uint8_t *rp = rsa_plain;
  lp1(&rp, 1); /* encryptionCheck */
  for (int i = 0; i < 4; i++)
    lp4(&rp, proto->xtea_seed[i]); /* xtea_seed */
  lp8(&rp, proto->session_id);     /* session_id */
  fprintf(stderr, "[DEBUG] login block session_id=0x%016llx\n",
          (unsigned long long)proto->session_id);
  lp1(&rp, 2);                         /* otp_type = none */
  lp4(&rp, 0);                         /* otp_value */
  lp1(&rp, (uint8_t)proto->auth_type); /* auth_type */
  ljstr(&rp, proto->secret);           /* password / session token */
  size_t rsa_plain_len = (size_t)(rp - rsa_plain);

  fprintf(stderr, "[DEBUG] RSA plaintext (%zu bytes): ", rsa_plain_len);
  for (size_t i = 0; i < rsa_plain_len; i++)
    fprintf(stderr, "%02x ", rsa_plain[i]);
  fprintf(stderr, "\n");

  uint8_t rsa_ct[256];
  size_t rsa_ct_len =
      osrs_rsa_encrypt_login(rsa_plain, rsa_plain_len, rsa_ct, sizeof(rsa_ct));
  if (rsa_ct_len == 0)
    return 0;

  /* --- Build XTEA payload --- */
  uint8_t xtea_payload[4096];
  memset(xtea_payload, 0, sizeof(xtea_payload));
  uint8_t *xp = xtea_payload;

  ljstr(&xp, proto->username);
  lp1(&xp, 0);       /* settings */
  lp2(&xp, 765);     /* width */
  lp2(&xp, 503);     /* height */
  memset(xp, 0, 24); /* uuid */
  xp += 24;
  ljstr(&xp, "");            /* siteSettings */
  lp4(&xp, 0);               /* affiliate */
  lp1(&xp, 0);               /* deep_link_count */
  write_platform_stats(&xp); /* platform_stats */
  lp1(&xp, 0);               /* secondClientType */
  lp4(&xp, 0);               /* reflectionCheckerConst */
  /* cache_crcs — order and encoding must match rsprot decodeCrc() */
  lp4_alt2(&xp, proto->index_crcs[18]);
  lp4_alt2(&xp, proto->index_crcs[19]);
  lp4_default(&xp, proto->index_crcs[22]);
  lp4_alt1(&xp, proto->index_crcs[12]);
  lp4_alt1(&xp, proto->index_crcs[9]);
  lp4_alt2(&xp, proto->index_crcs[1]);
  lp4_alt2(&xp, proto->index_crcs[17]);
  lp4_alt3(&xp, proto->index_crcs[16]);
  lp4_alt3(&xp, proto->index_crcs[7]);
  lp4_alt1(&xp, proto->index_crcs[13]);
  lp4_default(&xp, proto->index_crcs[3]);
  lp4_alt2(&xp, proto->index_crcs[5]);
  lp4_alt3(&xp, proto->index_crcs[10]);
  lp4_default(&xp, proto->index_crcs[11]);
  lp4_alt2(&xp, proto->index_crcs[2]);
  lp4_alt1(&xp, proto->index_crcs[8]);
  lp4_alt3(&xp, proto->index_crcs[0]);
  lp4_default(&xp, proto->index_crcs[21]);
  lp4_alt3(&xp, proto->index_crcs[4]);
  lp4_alt2(&xp, proto->index_crcs[20]);
  lp4_alt1(&xp, proto->index_crcs[6]);
  lp4_alt3(&xp, proto->index_crcs[14]);
  lp4_alt1(&xp, proto->index_crcs[15]);

  size_t xtea_len = (size_t)(xp - xtea_payload);
  size_t xtea_padded = xtea_len;
  if (xtea_padded % 8 != 0)
    xtea_padded += 8 - (xtea_padded % 8);

  fprintf(stderr, "[DEBUG] XTEA payload raw=%zu padded=%zu\n", xtea_len,
          xtea_padded);
  fprintf(stderr, "[DEBUG] XTEA first 64: ");
  for (size_t i = 0; i < xtea_len && i < 64; i++)
    fprintf(stderr, "%02x ", xtea_payload[i]);
  fprintf(stderr, "\n");

  osrs_xtea_t xtea_ctx;
  osrs_xtea_init_u32(&xtea_ctx, proto->xtea_seed);
  osrs_xtea_encrypt(&xtea_ctx, xtea_payload, xtea_padded);

  /* --- Assemble packet --- */
  size_t pos = 0;
  out[pos++] = 16; /* opcode */
  size_t block_len = 15 + 2 + rsa_ct_len + xtea_padded;
  out[pos++] = (uint8_t)(block_len >> 8);
  out[pos++] = (uint8_t)block_len;

  /* Plain header */
  out[pos++] = (uint8_t)(239 >> 24);
  out[pos++] = (uint8_t)(239 >> 16);
  out[pos++] = (uint8_t)(239 >> 8);
  out[pos++] = (uint8_t)239;
  out[pos++] = 0; /* subVersion */
  out[pos++] = 0;
  out[pos++] = 0;
  out[pos++] = 1; /* subVersion = 1 */
  out[pos++] = (uint8_t)(239 >> 24);
  out[pos++] = (uint8_t)(239 >> 16);
  out[pos++] = (uint8_t)(239 >> 8);
  out[pos++] = (uint8_t)239; /* serverVersion = 239 */
  out[pos++] = 1;            /* clientType */
  out[pos++] = 0;            /* platformType */
  out[pos++] = 0;            /* externalAuthenticatorType */

  /* RSA block with length prefix */
  out[pos++] = (uint8_t)(rsa_ct_len >> 8);
  out[pos++] = (uint8_t)rsa_ct_len;
  memcpy(&out[pos], rsa_ct, rsa_ct_len);
  pos += rsa_ct_len;

  /* XTEA payload */
  memcpy(&out[pos], xtea_payload, xtea_padded);
  pos += xtea_padded;

  return pos;
}

int osrs_login_parse_response(osrs_protocol_t *proto, const uint8_t *data,
                              size_t len, size_t *consumed) {
  *consumed = 0;
  if (len < 1)
    return -1;
  uint8_t code = data[0];

  if (code == OSRS_LOGINRESP_PROOF_OF_WORK) {
    if (len < 3)
      return -1;
    uint16_t plen = ((uint16_t)data[1] << 8) | data[2];
    if (len < 3 + plen)
      return -1;
    size_t p = 3;
    /* challengeType.id (1 byte) — always 0 for SHA256 */
    if (plen >= 1)
      p++;
    if (plen >= 2)
      proto->pow_version = data[p++];
    if (plen >= 3)
      proto->pow_difficulty = data[p++];
    /* salt is jstr (NUL-terminated) */
    size_t salt_start = p;
    while (p < 3 + plen && data[p] != 0)
      p++;
    size_t salt_len = p - salt_start;
    if (salt_len >= sizeof(proto->pow_salt))
      salt_len = sizeof(proto->pow_salt) - 1;
    memcpy(proto->pow_salt, data + salt_start, salt_len);
    proto->pow_salt[salt_len] = '\0';
    *consumed = 3 + plen;
    return code;
  }

  if (code == OSRS_LOGINRESP_OK) {
    if (len < 2)
      return -1;
    uint8_t plen = data[1];
    if (len < 2 + plen)
      return -1;
    size_t p = 2;
    uint8_t auth_flag = 0;
    if (plen >= 1)
      auth_flag = data[p++];
    if (plen >= 5)
      p += 4; /* skip unknown */
    if (plen >= 6)
      proto->staff_mod_level = data[p++];
    if (plen >= 7)
      proto->player_mod = data[p++] != 0;
    if (plen >= 9) {
      proto->player_index = ((int)data[p] << 8) | data[p + 1];
      p += 2;
    }
    if (plen >= 10)
      proto->members = data[p++] != 0;
    if (plen >= p + 8) {
      proto->account_hash =
          ((uint64_t)data[p] << 56) | ((uint64_t)data[p + 1] << 48) |
          ((uint64_t)data[p + 2] << 40) | ((uint64_t)data[p + 3] << 32) |
          ((uint64_t)data[p + 4] << 24) | ((uint64_t)data[p + 5] << 16) |
          ((uint64_t)data[p + 6] << 8) | (uint64_t)data[p + 7];
      p += 8;
    }
    if (plen >= p + 8) {
      proto->user_id =
          ((uint64_t)data[p] << 56) | ((uint64_t)data[p + 1] << 48) |
          ((uint64_t)data[p + 2] << 40) | ((uint64_t)data[p + 3] << 32) |
          ((uint64_t)data[p + 4] << 24) | ((uint64_t)data[p + 5] << 16) |
          ((uint64_t)data[p + 6] << 8) | (uint64_t)data[p + 7];
      p += 8;
    }
    if (plen >= p + 8) {
      proto->user_hash =
          ((uint64_t)data[p] << 56) | ((uint64_t)data[p + 1] << 48) |
          ((uint64_t)data[p + 2] << 40) | ((uint64_t)data[p + 3] << 32) |
          ((uint64_t)data[p + 4] << 24) | ((uint64_t)data[p + 5] << 16) |
          ((uint64_t)data[p + 6] << 8) | (uint64_t)data[p + 7];
      p += 8;
    }

    /* Initialize ISAAC ciphers */
    uint32_t in_seed[4];
    for (int i = 0; i < 4; i++)
      in_seed[i] = proto->xtea_seed[i] + 50;
    osrs_isaac_init(&proto->isaac_out, proto->xtea_seed);
    osrs_isaac_init(&proto->isaac_in, in_seed);

    if (auth_flag == 1) {
      for (int i = 0; i < 4; i++)
        (void)osrs_isaac_next(&proto->isaac_in);
    }

    *consumed = p;
    return code;
  }

  /* Other error codes: single byte */
  *consumed = 1;
  return code;
}

size_t osrs_login_build_pow_reply(uint64_t result, uint8_t *out) {
  out[0] = 19;
  out[1] = 0;
  out[2] = 8;
  out[3] = (uint8_t)(result >> 56);
  out[4] = (uint8_t)(result >> 48);
  out[5] = (uint8_t)(result >> 40);
  out[6] = (uint8_t)(result >> 32);
  out[7] = (uint8_t)(result >> 24);
  out[8] = (uint8_t)(result >> 16);
  out[9] = (uint8_t)(result >> 8);
  out[10] = (uint8_t)result;
  return 11;
}
