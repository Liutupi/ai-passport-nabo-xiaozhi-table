#pragma once
// NABO TABLE rules: cover menu, hand-off screens, 21 points and liar's dice for two to four
// players and "who is the undercover" for three to eight, all passing one badge around. Pure logic, a port of nabo-table/prototype/index.html: the same seed and
// the same key presses give the same deals, rolls and results. No display, audio or RTOS here.
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NT_MIN_PLAYERS 2
#define NT_MAX_PLAYERS 4
#define NT_MAX_HAND 12   // 4 aces, 4 twos, 3 threes reach 21; one more card busts
#define NT_MAX_DICE 6
#define NT_MAX_BIDS 160  // bids strictly increase over 24 quantities x 6 faces
#define NT_MAX_QTY 25    // "open" + quantities up to 4 x 6 dice
#define NT_UC_MIN 3      // who is the undercover: players
#define NT_UC_MAX 8
#define NT_UC_RECENT 24  // pairs not dealt again until this many others have been

typedef enum { NT_KEY_UP, NT_KEY_DOWN, NT_KEY_OK, NT_KEY_OK_LONG } nt_key_t;

typedef enum {
    NT_MENU, NT_HANDOFF, NT_BJ_DEAL, NT_BJ_TURN, NT_BJ_REVEAL, NT_DICE_ROLL, NT_DICE_TURN, NT_DICE_REVEAL, NT_END,
    NT_UC_SETUP, NT_UC_WORD, NT_UC_TALK, NT_UC_VOTE, NT_UC_OUT, NT_UC_END
} nt_scene_t;

typedef enum { NT_GAME_NONE, NT_GAME_BJ, NT_GAME_DICE, NT_GAME_UC } nt_game_t;
// NT_MENU_DUEL hands the badge over to NABO DUEL (nabo_duel.c); the table model only raises NT_EV_DUEL.
typedef enum { NT_MENU_BJ, NT_MENU_DICE, NT_MENU_UC, NT_MENU_DUEL, NT_MENU_PLAYERS, NT_MENU_TARGET, NT_MENU_COUNT } nt_menu_item_t;

// NABO's dealer animations. The renderer picks the sprite (e.g. SHUFFLE is the card spread on
// the cover and the cut at the start of a round; POINT with mirror set means point right).
typedef enum {
    NT_ANIM_IDLE, NT_ANIM_SHUFFLE, NT_ANIM_DEAL, NT_ANIM_SHAKE, NT_ANIM_SLAM, NT_ANIM_COVER_IN, NT_ANIM_COVER,
    NT_ANIM_OPEN, NT_ANIM_POINT, NT_ANIM_SURPRISE, NT_ANIM_TROPHY, NT_ANIM_SHRUG, NT_ANIM_COUNT
} nt_anim_t;

// Events raised since the last nt_take_events() call, for sound.
enum {
    NT_EV_MOVE = 1 << 0,
    NT_EV_OK = 1 << 1,
    NT_EV_CARD = 1 << 2,      // a card slides out
    NT_EV_HIT = 1 << 3,       // the player drew a card: slide now, flip ~130 ms later
    NT_EV_FLIP = 1 << 4,
    NT_EV_SHUFFLE = 1 << 5,
    NT_EV_RATTLE = 1 << 6,    // dice shaken in the cup
    NT_EV_SLAM = 1 << 7,      // cup put down (the prototype plays it 180 ms after the slam starts)
    NT_EV_OPEN = 1 << 8,
    NT_EV_LIFT = 1 << 9,      // cups lifted at the reveal
    NT_EV_WIN = 1 << 10,
    NT_EV_SETTINGS = 1 << 11, // the player count or the target changed: persist them
    NT_EV_DUEL = 1 << 12,     // "对决 · 联机" chosen on the cover: switch to NABO DUEL
};

typedef struct {
    uint8_t target;     // 21 points: rounds needed to win; liar's dice: penalties that lose. 3, 5 or 7
    uint8_t players;    // 2..4, chosen on the cover
    uint8_t uc_players; // who is the undercover: 3..8, chosen on its setup screen
    uint8_t dice_per;   // dice per player, 3..6
    uint8_t min_open;   // smallest quantity a bid may use (raised to the player count)
    bool wild;          // ones count as any face until someone bids ones ("zhai")
    float anim_s[NT_ANIM_COUNT];  // one-shot lengths in seconds; 0 = built-in default
} nt_config_t;

typedef struct { uint8_t r, s; double born; } nt_card_t;   // rank 1..13, suit 0..3 (odd = red)
typedef struct { uint8_t p, q, f; } nt_bid_t;
typedef struct { int total, hard; bool soft, bust; } nt_total_t;

typedef enum { NT_BJ_CHOOSE, NT_BJ_HIT_SHOW } nt_bj_phase_t;
typedef enum { NT_STEP_QTY, NT_STEP_FACE } nt_bid_step_t;

typedef struct {
    uint8_t deck[52][2];
    uint8_t deck_n;
    nt_card_t hand[NT_MAX_PLAYERS][NT_MAX_HAND];
    uint8_t hand_n[NT_MAX_PLAYERS];
    bool stood[NT_MAX_PLAYERS];
    nt_bj_phase_t phase;
    uint8_t cursor;     // 0 = hit, 1 = stand
    uint8_t dealt;
} nt_bj_t;

typedef struct {
    uint8_t dice[NT_MAX_PLAYERS][NT_MAX_DICE];
    float yaw[NT_MAX_PLAYERS][NT_MAX_DICE];   // display angle of each die, degrees
    uint8_t per, min_open;
    bool wild;
    nt_bid_t bids[NT_MAX_BIDS];
    uint8_t bid_n;
    nt_bid_step_t step;
    uint8_t qlist[NT_MAX_QTY];   // 0 = "open", else a quantity
    uint8_t qn, qi;
    uint8_t flist[7];            // 0 = "back", else a face
    uint8_t fn, fi;
    int8_t caller;
} nt_dice_t;

// Who is the undercover. Everyone gets a word; the undercovers (1, or 2 from seven players)
// get the other word of the pair. Players describe their word out loud, then vote someone out
// on the badge. Civilians win when every undercover is out; undercovers win once they are at
// least as many as the civilians left.
typedef enum { NT_UC_LIST_VOTE, NT_UC_LIST_FORGOT } nt_uc_list_t;

typedef struct {
    uint8_t n, n_uc;
    uint16_t pair;
    bool swap;                    // the undercovers get word 0 of the pair, civilians word 1
    bool uc[NT_UC_MAX], alive[NT_UC_MAX];
    uint8_t round, speaker, out;  // first to describe this round; the player just voted out
    uint8_t cursor;               // setup: player count; talk: 0 vote, 1 someone forgot; lists: row
    nt_uc_list_t list;
    bool confirm;                 // vote: "vote out player N?" step
    uint8_t confirm_cursor;       // 0 = yes, 1 = cancel
    bool recheck;                 // the word screen was opened from "someone forgot"
    bool uc_won;                  // valid once the game is over
    uint16_t recent[NT_UC_RECENT];
    uint8_t recent_n;
} nt_uc_t;

typedef struct {
    bool has;
    uint8_t winners;             // bit per player; 0 = tie, nobody scores
    int8_t loser;                // liar's dice: who takes the penalty point; -1 in 21 points
    // 21 points: every total; liar's dice: the counted faces against the last bid.
    nt_total_t bj[NT_MAX_PLAYERS];
    uint8_t actual, bid_q, bid_f;
    bool bid_held;
} nt_result_t;

typedef struct {
    nt_config_t cfg;
    uint32_t rng;
    double t, clock;             // seconds in the current scene / since init
    nt_scene_t scene;
    nt_game_t game;
    uint8_t menu;
    uint8_t np;                  // players in this match (cfg.players when it started)
    uint8_t score[NT_MAX_PLAYERS];   // 21 points: rounds won; liar's dice: penalty points
    uint8_t round, starter, loser, turn, handoff_to;
    bool paused;
    uint8_t pause_cursor;        // 0 = continue, 1 = back to cover
    uint8_t choice;              // result banner / end screen: 0 = next, 1 = back to cover
    uint8_t sfx_mark;
    nt_bj_t bj;
    nt_dice_t dc;
    nt_uc_t uc;
    nt_result_t result;
    nt_anim_t anim, anim_next;
    bool mirror, has_next;
    double anim_t;
    uint32_t events;
} nt_t;

void nt_init(nt_t *g, const nt_config_t *cfg, uint32_t seed);
void nt_key(nt_t *g, nt_key_t key);
void nt_update(nt_t *g, double dt);
uint32_t nt_take_events(nt_t *g);

nt_total_t nt_hand_total(const nt_card_t *hand, int n);
bool nt_bj_can_hit(const nt_t *g, int p);   // not bust, and the deck still has a card
bool nt_can_pause(const nt_t *g);
bool nt_match_over(const nt_t *g);
// The player the match is about: 21 points, the leader; liar's dice, the most penalised.
int nt_match_focus(const nt_t *g);
int nt_next_player(const nt_t *g, int p);
// Seats: 2 players sit left and right; 3 or 4 sit in a 2 x 2 grid (the third alone, centred).
// Odd players sit on the right, so NABO points right at them.
static inline bool nt_seat_right(int p) { return p & 1; }
int nt_popcount(unsigned v);
const nt_bid_t *nt_last_bid(const nt_t *g);
bool nt_zhai(const nt_t *g);
int nt_count_face(const nt_t *g, int face);
bool nt_wild_counts(const nt_t *g, int face);
// Reveal animation helpers shared by the renderer: flip progress of card i of player p.
float nt_bj_flip(const nt_t *g, int p, int i);
float nt_anim_len(const nt_t *g, nt_anim_t a);

// Who is the undercover (nabo_table_uc.c).
void nt_uc_setup(nt_t *g);              // cover -> setup screen
void nt_uc_key(nt_t *g, nt_key_t key);  // keys in the undercover scenes
void nt_uc_begin_turn(nt_t *g, int p);  // after a hand-off: show player p their word
const char *nt_uc_word(const nt_t *g, int p);
int nt_uc_alive(const nt_t *g, bool undercover);   // players still in, of one side
int nt_uc_list_len(const nt_t *g);      // rows in the vote / forgot list (alive players + one)
int nt_uc_list_player(const nt_t *g, int row);     // player on a list row, -1 for the last row

// Liar's dice internals (nabo_table_bid.c), used by the model.
void nt_dice_new_round(nt_t *g);
void nt_dice_build_bids(nt_t *g);
void nt_dice_key(nt_t *g, nt_key_t key);
void nt_dice_reveal_update(nt_t *g);

// Shared helpers (nabo_table_model.c).
double nt_rand(nt_t *g);
void nt_go(nt_t *g, nt_scene_t scene);
void nt_play(nt_t *g, nt_anim_t a, bool mirror);
void nt_handoff(nt_t *g, int p);
// Scores the round: winners (bit per player) gain a round in 21 points; in liar's dice the
// loser gains a penalty point. winners == 0 and loser < 0 is a tie.
void nt_finish_round(nt_t *g, unsigned winners, int loser);
void nt_to_menu(nt_t *g);
int nt_rand_int(nt_t *g, int n);

#ifdef __cplusplus
}
#endif
