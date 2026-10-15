#ifndef POKERED_HM_H
#define POKERED_HM_H

#define PK_HM_WAIT_STEPS 3
#define PK_HM_PRESS_TRIES 3
#define PK_HM_NAV_STEPS 8
#define PK_HM_SETTLE_STEPS 24

static const uint8_t PK_HM_BADGE_BIT[PK_NUM_HMS] = {PKRED_BADGE_BIT_CASCADE, PKRED_BADGE_BIT_THUNDER,
                                                    PKRED_BADGE_BIT_SOUL, PKRED_BADGE_BIT_RAINBOW,
                                                    PKRED_BADGE_BIT_BOULDER};
static const uint8_t PK_HM_FIELD_ID[PK_NUM_HMS] = {1, 2, 4, 5, 6};

static uint8_t tile_in_front(const PkSnapshot *s) {
  static const uint8_t col[4] = {8, 8, 6, 10}, row[4] = {11, 7, 9, 9};
  int f = s->facing & 3;
  return s->tile_map[row[f] * PK_TILE_MAP_W + col[f]];
}

static bool tiles_overworld(const uint8_t *tiles) {
  for (int i = 0; i < PK_TILE_MAP_CELLS; i++)
    if (tiles[i] >= PKRED_TILE_TEXT_FIRST)
      return false;
  return true;
}

static int hm_party_slot(const PkSnapshot *s, int h) {
  for (int i = 0; i < s->party_count && i < PARTY_SIZE; i++)
    for (int m = 0; m < 4; m++)
      if (s->party[i].moves[m] == PK_HM_MOVES[h])
        return i;
  return -1;
}

static bool hm_can_surf(const PkSnapshot *s) {
  bool water_tileset = false;
  for (size_t i = 0; i < sizeof(PKRED_WATER_TILESETS); i++)
    water_tileset |= PKRED_WATER_TILESETS[i] == s->tileset;
  uint8_t t = tile_in_front(s);
  bool shore = s->tileset != PKRED_TILESET_SHIP_PORT && (t == PKRED_TILE_SHORE || t == PKRED_TILE_SAFARI_SHORE);
  return water_tileset && !s->surfing && (t == PKRED_TILE_WATER || shore);
}

static bool hm_action_ready(const PkSnapshot *s, int h) {
  if (s->in_battle || !tiles_overworld(s->tile_map) || !((s->badges >> PK_HM_BADGE_BIT[h]) & 1) ||
      hm_party_slot(s, h) < 0)
    return false;
  uint8_t t = tile_in_front(s);
  switch (PK_HM_MOVES[h]) {
  case PKRED_MOVE_CUT:
    return s->tileset == PKRED_TILESET_OVERWORLD ? t == PKRED_TILE_CUT_TREE || t == PKRED_CUT_TILE_GRASS
           : s->tileset == PKRED_TILESET_GYM     ? t == PKRED_TILE_GYM_CUT_TREE
                                                 : false;
  case PKRED_MOVE_FLY:
    return s->tileset == PKRED_TILESET_OVERWORLD || s->tileset == PKRED_TILESET_PLATEAU;
  case PKRED_MOVE_SURF:
    return hm_can_surf(s);
  case PKRED_MOVE_STRENGTH:
    return !s->strength_active;
  case PKRED_MOVE_FLASH:
    return s->dark_cave;
  }
  return false;
}

typedef struct {
  Env *env;
  PkRam ram;
  uint8_t cut_fired;
} PkHmRun;

typedef bool (*PkHmMenuFn)(PkHmRun *r);

static uint8_t hm_rd(PkHmRun *r, uint16_t addr) { return r->ram.read(r->ram.ctx, addr); }

static void hm_press(PkHmRun *r, unsigned buttons) {
  env_advance_buttons(r->env, buttons);
  if (r->env->tracker.cut_fired)
    r->cut_fired = r->env->tracker.cut_fired;
}

static bool hm_in_start_menu(PkHmRun *r) {
  return hm_rd(r, PKRED_ADDR_TOP_MENU_ITEM_Y) == PKRED_START_MENU_Y &&
         hm_rd(r, PKRED_ADDR_TOP_MENU_ITEM_X) == PKRED_START_MENU_X &&
         hm_rd(r, PKRED_ADDR_MENU_WATCHED_KEYS) == PKRED_START_MENU_KEYS;
}

static bool hm_in_party_menu(PkHmRun *r) {
  return hm_rd(r, PKRED_ADDR_TOP_MENU_ITEM_Y) == PKRED_PARTY_MENU_Y && hm_rd(r, PKRED_ADDR_TOP_MENU_ITEM_X) == 0 &&
         hm_rd(r, PKRED_ADDR_MENU_WATCHED_KEYS) == PKRED_MENU_KEYS_A_B;
}

static bool hm_in_field_menu(PkHmRun *r) {
  return hm_rd(r, PKRED_ADDR_TOP_MENU_ITEM_X) != 0 && hm_rd(r, PKRED_ADDR_MENU_WATCHED_KEYS) == PKRED_MENU_KEYS_A_B;
}

static bool hm_idle(PkHmRun *r) {
  uint8_t tiles[PK_TILE_MAP_CELLS];
  for (int i = 0; i < PK_TILE_MAP_CELLS; i++)
    tiles[i] = hm_rd(r, (uint16_t)(PKRED_ADDR_TILE_MAP + i));
  return hm_rd(r, PKRED_ADDR_IS_IN_BATTLE) == 0 && tiles_overworld(tiles);
}

static bool hm_wait(PkHmRun *r, PkHmMenuFn done) {
  for (int i = 0; i < PK_HM_WAIT_STEPS; i++) {
    if (done(r))
      return true;
    hm_press(r, 0);
  }
  return done(r);
}

/* Closed loop: a press the menu missed (still drawing) is retried, and A is re-pressed only
   while the menu's own variables are still in place, so an accepted A is never doubled. With no
   next menu (the field-move menu: Fly's town map and result text keep its variables) A is pressed
   once; that menu is already taking input when its variables appear. */
static bool hm_select(PkHmRun *r, int target, PkHmMenuFn menu, PkHmMenuFn next) {
  for (int i = 0; i < PK_HM_NAV_STEPS && menu(r); i++) {
    int cur = hm_rd(r, PKRED_ADDR_CURRENT_MENU_ITEM);
    if (cur == target)
      break;
    hm_press(r, cur < target ? PK_BTN_DOWN : PK_BTN_UP);
  }
  for (int i = 0; i < PK_HM_PRESS_TRIES; i++) {
    if (!menu(r) || hm_rd(r, PKRED_ADDR_CURRENT_MENU_ITEM) != target)
      return false;
    hm_press(r, PK_BTN_A);
    if (!next)
      return true;
    for (int w = 0; w < PK_HM_WAIT_STEPS && menu(r); w++)
      hm_press(r, 0);
    if (!menu(r))
      return hm_wait(r, next);
  }
  return false;
}

static int hm_field_menu_index(PkHmRun *r, int h) {
  for (int i = 0; i < PKRED_NUM_FIELD_MOVES; i++)
    if (hm_rd(r, (uint16_t)(PKRED_ADDR_FIELD_MOVES + i)) == PK_HM_FIELD_ID[h])
      return i;
  return -1;
}

static void hm_settle(PkHmRun *r) {
  for (int i = 0; i < PK_HM_SETTLE_STEPS && !hm_idle(r); i++)
    hm_press(r, PK_BTN_B);
}

static bool hm_open_field_menu(PkHmRun *r, int slot) {
  r->ram.write(r->ram.ctx, PKRED_ADDR_TOP_MENU_ITEM_Y, 0);
  bool open = false;
  for (int i = 0; i < PK_HM_PRESS_TRIES && !open; i++) {
    hm_press(r, PK_BTN_START);
    open = hm_wait(r, hm_in_start_menu);
  }
  if (!open)
    return false;
  int pokemon = hm_rd(r, PKRED_ADDR_MAX_MENU_ITEM) == PKRED_START_MENU_MAX_DEX ? 1 : 0;
  return hm_select(r, pokemon, hm_in_start_menu, hm_in_party_menu) &&
         hm_select(r, slot, hm_in_party_menu, hm_in_field_menu);
}

/* Runs Start > POKEMON > mon > HM inside one env step, then dismisses the result text and
   menus. Fly stops on the town map once it takes input (its arrows are drawn just before the
   input loop), so the policy picks the destination. */
static void hm_run(Env *env, int h) {
  PkHmRun r = {env, env_ram(env), 0};
  int slot = hm_party_slot(&env->cur, h);
  if (hm_open_field_menu(&r, slot)) {
    int index = hm_field_menu_index(&r, h);
    if (index >= 0 && hm_select(&r, index, hm_in_field_menu, NULL) && PK_HM_MOVES[h] == PKRED_MOVE_FLY) {
      for (int i = 0; i < PK_HM_SETTLE_STEPS && hm_rd(&r, PKRED_ADDR_TILE_MAP + PKRED_FLY_MAP_ARROW_X) != PKRED_TILE_UP_ARROW; i++)
        hm_press(&r, 0);
      env->tracker.cut_fired = r.cut_fired;
      return;
    }
  }
  hm_settle(&r);
  env->tracker.cut_fired = r.cut_fired;
}

#endif
