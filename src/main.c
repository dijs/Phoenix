#include <pebble.h>

#define ACCEL_STEP_MS 30
#define STEPS_IN_SECOND 1000 / ACCEL_STEP_MS

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define ABS(a) ((a > 0) ? (a) : (-a))

#define CREEP_INITIAL_SCORE 10
#define EXTRA_LIFE_SELECTION 0
#define POWER_UP_SELECTION 1
#define DOUBLE_GUN_SELECTION 2
#define TRIPLE_GUN_SELECTION 3
#define DONE_SELECTION 4
#define DEFAULT_GUN 0
#define DOUBLE_GUN 1
#define TRIPLE_GUN 2
#define INITIAL_SHIP_ARMOR 4
#define INITIAL_READY_COUNT 3
#define SHIP_MOVEMENT_SPEED 2
#define SHIP_FIRE_TIME_LAG 10
#define BULLET_RADIUS 1
#define BULLET_WIDTH 2
#define MAX_PLAYER_BULLETS 64
#define MAX_CREEP_BULLETS 64
#define ACCEL_MID 16 // started with 32...
#define INITIAL_MONEY 100
#define INITIAL_GUN_POWER 1
#define ASCII_ZERO 48
#define WALL 0
#define DISTANCE 1

// test a lot!

typedef enum { LevelState, StoreState, GetReadyState, GameOverState, TipState } GameState;

Window* window;
Layer* windowLayer;
Layer* layer;
GBitmap* ship;
GBitmap* creepBitmap[4];
GBitmap* tipBitmap;
AppTimer* timer;
GRect windowBounds;
GRect shipBounds;
GRect bulletBounds;
AccelData accelData;

int padding = 8;
int possibleNextPosition;
int rightWall, leftWall, topWall, bottomWall;
int bottom;

int lastPlayerFireTime = 0;

int creepScore = CREEP_INITIAL_SCORE;
int creepsLeft;
int creepHealthMultiplier = 1;

char moneyText[12];
char levelText[8];

int gameTime = 0;
char readyText[4];
int readyStepsLeft = STEPS_IN_SECOND;
int readyCount = INITIAL_READY_COUNT;
int storeSelectionCosts[4] = {100, 200, 300, 500};
int storeSelection = 0;
bool isPaused = false;

typedef struct {
  bool visible;
  GPoint pos;
  GPoint vel;
} Bullet;

typedef struct {
  GPoint delta;
  int conditionType;
  int distance;
} MovementRule;

typedef struct {
  int health;
  GPoint initialPosition;
  GRect bounds;
  MovementRule* rules;
  int ruleCount;
  int currentRule;
  int traveled;
  int fullHealth;
  int type;
} Creep;

typedef struct {
  Creep* creeps;
  int creepCount;
} Level;

typedef struct {
  GameState state;
  Level* levels;
  int levelCount;
  int currentLevel;
} Game;

Bullet playerBullets[MAX_PLAYER_BULLETS];
Bullet creepBullets[MAX_CREEP_BULLETS];

typedef struct {
  int armor;
  int fullArmor;
  int money;
} Player;

int gunType = DEFAULT_GUN;
int currentPlayerBullet = 0;
int currentGunPower = INITIAL_GUN_POWER;
int currentCreepBullet = 0;

GPoint weakPoints[10] = {
  {0,4}, {1,1}, {1,7}, {2,5}, {3,4},
  {3,7}, {4,0}, {5,4}, {6,1}, {6,7}
};

Game game;
Player player;

void forEachPlayerBullet(void (*f)(Bullet*)){
  for(int index = 0; index < MAX_PLAYER_BULLETS; index++)
    (*f)(&playerBullets[index]);
}

void forEachCreepBullet(void (*f)(Bullet*)){
  for(int index = 0; index < MAX_CREEP_BULLETS; index++)
    (*f)(&creepBullets[index]);
}

void updateShipPosition(){
  accel_service_peek(&accelData);
  if(ABS(accelData.x) > ACCEL_MID){
    int movement = accelData.x < 0 ? -SHIP_MOVEMENT_SPEED : SHIP_MOVEMENT_SPEED;
    possibleNextPosition = shipBounds.origin.x + movement;
    shipBounds.origin.x = MAX(MIN(possibleNextPosition, rightWall), padding);
  }
}

void updateBullet(Bullet* bullet){
  if(bullet->visible){
    if(grect_contains_point(&windowBounds, &bullet->pos)){
      bullet->pos.x += bullet->vel.x;
      bullet->pos.y += bullet->vel.y;
    }else{
      bullet->visible = false;
    }
  }
}

// Might want to use a bitvector to assign visible or not...
// Update velocity for special types, will need time...
// position might need ot be float...  

void fireBullet(Bullet* bullet, int x, int y, int vx, int vy){
  bullet->visible = true;
  bullet->pos.x = x;
  bullet->pos.y = y;
  bullet->vel.x = vx;
  bullet->vel.y = vy;
}

void firePlayerGunAt(int x, int y, int vx, int vy){
  currentPlayerBullet = (currentPlayerBullet + 1) % MAX_PLAYER_BULLETS;
  fireBullet(&playerBullets[currentPlayerBullet], x, y, vx, vy);
}

void fireCreepGun(Creep* creep, int vx, int vy){
  currentCreepBullet = (currentCreepBullet + 1) % MAX_CREEP_BULLETS;
  fireBullet(
    &creepBullets[currentCreepBullet], 
    creep->bounds.origin.x + creep->bounds.size.w / 2,
    creep->bounds.origin.y + creep->bounds.size.h,
    vx,
    vy
  );
}

void firePlayerGun(){
  bool isTriple = gunType == TRIPLE_GUN;
  bool isDouble = gunType == DOUBLE_GUN;
  bool isDefault = gunType == DEFAULT_GUN;
  int x = shipBounds.origin.x + ship->bounds.size.w / 2;
  if(isDefault || isTriple){
    firePlayerGunAt(x, bottom, 0, -1);
  }
  if(isDouble || isTriple){
    firePlayerGunAt(x - 2, bottom, -1, -1);
    firePlayerGunAt(x + 2, bottom, 1, -1);
  }
  // Change this for gun speed power up
  lastPlayerFireTime = SHIP_FIRE_TIME_LAG;
}

bool playerGunReady(){
  return !lastPlayerFireTime--;
}

bool isPlayerWeak(){
  return player.armor == 0;
}

void hideBullet(Bullet* bullet){
  bullet->visible = false;
}

Level* getCurrentLevel(){
  return &game.levels[game.currentLevel % game.levelCount];  
}

void resetCreepMovement(Creep* creep){
  creep->currentRule = 0;
  creep->traveled = 0;
  creep->bounds.origin.x = creep->initialPosition.x;
  creep->bounds.origin.y = creep->initialPosition.y; 
}

void resetLevel(){
  Level* level = getCurrentLevel();
  creepsLeft = level->creepCount;
  for(int index = 0; index < level->creepCount; index++){
    Creep* creep = &level->creeps[index];
    creep->health = creep->fullHealth * creepHealthMultiplier;
    resetCreepMovement(creep);
  }
  forEachPlayerBullet(hideBullet);
  forEachCreepBullet(hideBullet);
  player.armor = player.fullArmor;
}

void handleLevelWin(){
  game.currentLevel++;
  creepScore += 10;
  storeSelection = 0;
  if(game.currentLevel % game.levelCount == 0) creepHealthMultiplier++;
  game.state = StoreState;
}

bool isCreepAlive(Creep* creep){
  return creep->health > 0;
}

bool isCreepWeak(Creep* creep){
  return creep->health == 1;
}

bool checkForCreepHit(Creep* creep){
  for(int index = 0; index < MAX_PLAYER_BULLETS; index++){
    Bullet* bullet = &playerBullets[index];
    if(bullet->visible){
      if(grect_contains_point(&creep->bounds, &bullet->pos)){
        // Reset bullet on hit
        bullet->visible = false;
        // Hurt creep
        creep->health -= currentGunPower;
        if(!isCreepAlive(creep)){
          // Creep killed
          player.money += creepScore;
          // Did we win the level?
          if(--creepsLeft == 0) handleLevelWin();
        }
        return true;
      }
    }
  }
  return false;
}

bool creepShouldFire(Creep* creep){
  return creep->health > 0 && (rand() % 1000) < 10;
}

void handlePlayerHit(Bullet* bullet){
  if(--player.armor == -1) game.state = GameOverState;
  hideBullet(bullet);
}

void checkForPlayerHit(Bullet* bullet){
  // Check if creep bullet hit our ship
  if(bullet->visible && grect_contains_point(&shipBounds, &bullet->pos)){
    handlePlayerHit(bullet);
  }
}

bool creepOutsideBounds(Creep* creep){
  MovementRule* rule = &creep->rules[creep->currentRule];
  int cx = creep->bounds.origin.x;
  int cy = creep->bounds.origin.y;
  return (ABS(rule->delta.x) > 0 && (cx <= leftWall || cx >= rightWall)) ||
    (ABS(rule->delta.y) > 0 && (cy <= topWall || cy >= bottomWall));
}

void updateCreepMovement(Creep* creep){
  MovementRule* rule = &creep->rules[creep->currentRule];
  creep->bounds.origin.x += rule->delta.x;
  creep->bounds.origin.y += rule->delta.y;
  creep->traveled += ABS(rule->delta.x) + ABS(rule->delta.y);
  bool outsideWall = rule->conditionType == WALL && creepOutsideBounds(creep);
  bool atDistance = rule->conditionType == DISTANCE && creep->traveled > rule->distance;
  // Cycle to next rule if needed
  if(outsideWall || atDistance){
    creep->currentRule = (creep->currentRule + 1) % creep->ruleCount;
    creep->traveled = 0;
  }
  // check if we need a reset
  if(creep->bounds.origin.y > windowBounds.size.h){
    resetCreepMovement(creep);
  }
}

void updateCreeps(){
  Level* level = getCurrentLevel();
  for(int index = 0; index < level->creepCount; index++){
    Creep* creep = &level->creeps[index];
    if(isCreepAlive(creep)){
      updateCreepMovement(creep);
      checkForCreepHit(creep);
      if(creepShouldFire(creep)) fireCreepGun(creep, 0, 1);
    }
  }
}

void drawPlayerBullets(GContext* ctx){
  for(int i = 0; i < MAX_PLAYER_BULLETS; i++)
    if(playerBullets[i].visible)
      graphics_fill_circle(ctx, playerBullets[i].pos, BULLET_RADIUS);
}

void drawCreepBullets(GContext* ctx){
  for(int i = 0; i < MAX_CREEP_BULLETS; i++)
    if(creepBullets[i].visible)
      graphics_fill_circle(ctx, creepBullets[i].pos, BULLET_RADIUS);
}

void drawWeak(GContext* ctx, GPoint origin){
  graphics_context_set_stroke_color(ctx, GColorWhite);
  for(int i = 0; i < 10; i++)
    graphics_draw_pixel(ctx, GPoint(origin.x + weakPoints[i].x, origin.y + weakPoints[i].y));
}

void drawCreeps(GContext* ctx){
  Level* level = getCurrentLevel();
  for(int index = 0; index < level->creepCount; index++){
    Creep* creep = &level->creeps[index];
    if(isCreepAlive(creep)){
      graphics_draw_bitmap_in_rect(ctx, creepBitmap[creep->type], creep->bounds);
      if(isCreepWeak(creep)) drawWeak(ctx, creep->bounds.origin);
    }
  }
}

void drawText(GContext* ctx, const char* text, GRect rect){
  graphics_draw_text(ctx,
    text,
    fonts_get_system_font(FONT_KEY_GOTHIC_14),
    rect,
    GTextOverflowModeWordWrap,
    GTextAlignmentLeft,
    NULL
  );
}

void drawBoldText(GContext* ctx, const char* text, GRect rect){
  graphics_draw_text(ctx,
    text,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    rect,
    GTextOverflowModeWordWrap,
    GTextAlignmentLeft,
    NULL
  );
}

void drawScoreAndLevel(GContext* ctx){
  snprintf(moneyText, 12, "$%d", player.money);
  snprintf(levelText, 8, "Lvl %d", game.currentLevel + 1);
  drawText(ctx, levelText, GRect(2, 2, 64, 8));
  drawText(ctx, moneyText, GRect(windowBounds.size.w - 32, 2, 32, 8));
  if(game.state == GameOverState){
    drawBoldText(ctx, "Press select to play again", GRect(16, 32, 128, 32));
  }
}

void drawGetReady(GContext* ctx){
  snprintf(readyText, 4, "%d!", readyCount);
  drawBoldText(ctx, readyText, GRect(
    windowBounds.size.w / 2,
    windowBounds.size.h / 2,
    16,
    16
  ));
}

void drawStore(GContext* ctx){
  
  int y = 32;
  int left = 16;
  int right = windowBounds.size.w - 32;
  int lineHeight = 8;

  drawBoldText(ctx, "Store", GRect(2, 16, 64, lineHeight));

  // Current selection
  drawText(ctx, ">", GRect(2, 30 + (lineHeight * 2 * storeSelection), 8, lineHeight));

  drawText(ctx, "Armor", GRect(left, y, 64, lineHeight));
  drawText(ctx, "$100", GRect(right, y, 32, lineHeight));

  y += lineHeight * 2;

  drawText(ctx, "Power Up", GRect(left, y, 64, lineHeight));
  drawText(ctx, "$200", GRect(right, y, 32, lineHeight));

  y += lineHeight * 2;

  drawText(ctx, "Double Gun", GRect(left, y, 64, lineHeight));
  drawText(ctx, "$300", GRect(right, y, 32, lineHeight));

  y += lineHeight * 2;

  drawText(ctx, "Triple Gun", GRect(left, y, 64, lineHeight));
  drawText(ctx, "$500", GRect(right, y, 32, lineHeight));

  y += lineHeight * 2;

  drawText(ctx, "Done", GRect(left, y, 64, lineHeight));

}

void drawShip(GContext* ctx) {
  graphics_draw_bitmap_in_rect(ctx, ship, shipBounds);
  if(isPlayerWeak()) drawWeak(ctx, shipBounds.origin);
}

void drawArmorBar(GContext* ctx) {
  float ratio = (float)player.armor / (float)player.fullArmor;
  int w = (int)(ratio * (float)windowBounds.size.w);
  int h = 2;
  graphics_fill_rect(ctx, GRect(0, windowBounds.size.h - h, w, h), 0, GCornerNone);
}

void updateGetReady(){
  if(--readyStepsLeft == 0){
    if(--readyCount == 0){
      game.state = LevelState;
      readyCount = INITIAL_READY_COUNT;
    }
    readyStepsLeft = STEPS_IN_SECOND;
  }  
}

// Game loop
void timer_callback(void *data) {
  gameTime++;
  if(game.state == LevelState && !isPaused){
    forEachPlayerBullet(updateBullet);
    forEachCreepBullet(updateBullet);
    updateShipPosition();
    updateCreeps();
    forEachCreepBullet(checkForPlayerHit);
    if(playerGunReady()) firePlayerGun();
  }
  if(game.state == GetReadyState) updateGetReady();
  // Redraw
  layer_mark_dirty(layer);
  // Ask for another loop
  timer = app_timer_register(ACCEL_STEP_MS, timer_callback, NULL);
}

// Draw
void layer_update_callback(Layer *me, GContext* ctx) {
  graphics_context_set_text_color(ctx, GColorBlack);
  if(game.state == TipState){
    graphics_draw_bitmap_in_rect(ctx, tipBitmap, tipBitmap->bounds);
    drawText(ctx, "Press any button...", GRect(28, windowBounds.size.h - 32, 128, 8));
  }else{
    drawScoreAndLevel(ctx);
    if(game.state == LevelState || game.state == GetReadyState){
      drawShip(ctx);
      drawArmorBar(ctx);
      drawPlayerBullets(ctx);
      drawCreepBullets(ctx);
      drawCreeps(ctx);
    }
    if(game.state == GetReadyState) drawGetReady(ctx);
    if(game.state == StoreState) drawStore(ctx);
  }
}

void select_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  if(game.state == TipState){
    game.state = GetReadyState;
  }
  if(game.state == GameOverState){
    player.money = INITIAL_MONEY;
    player.armor = INITIAL_SHIP_ARMOR;
    game.currentLevel = 0;
    game.state = GetReadyState;
    gunType = DEFAULT_GUN;
    currentGunPower = INITIAL_GUN_POWER;
    creepScore = CREEP_INITIAL_SCORE;
    resetLevel();
  }
  if(game.state == LevelState){
    isPaused = !isPaused;
  }
  if(game.state == StoreState){
    if(storeSelection == DONE_SELECTION){
      game.state = GetReadyState;
      resetLevel();
    }else{
      if(player.money >= storeSelectionCosts[storeSelection]){
        // Make purchase
        player.money -= storeSelectionCosts[storeSelection];
        switch(storeSelection){
          case EXTRA_LIFE_SELECTION:
            player.fullArmor++;
            break;
          case POWER_UP_SELECTION:
            // Need max...
            currentGunPower++;
            break;
          case DOUBLE_GUN_SELECTION:
            gunType = DOUBLE_GUN;
            break;
          case TRIPLE_GUN_SELECTION:
            gunType = TRIPLE_GUN;
            break;
        }
      }else{
        // Does not have enough monies
        vibes_short_pulse();
      }
    }
  }
}

void up_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  if(game.state == StoreState){
    if(storeSelection == 0){
      storeSelection = 4;
    }else{
      storeSelection--;
    }
  }
  if(game.state == TipState){
    game.state = GetReadyState;
  }
}

void down_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  if(game.state == StoreState){
    if(storeSelection == 4){
      storeSelection = 0;
    }else{
      storeSelection++;
    }
  }
  if(game.state == TipState){
    game.state = GetReadyState;
  }
}

void config_provider(void *context) {
  window_set_click_context(BUTTON_ID_SELECT, context);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_single_click_handler);
  window_set_click_context(BUTTON_ID_UP, context);
  window_single_click_subscribe(BUTTON_ID_UP, up_single_click_handler);
  window_set_click_context(BUTTON_ID_DOWN, context);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_single_click_handler); 
}

int getBufferInt(uint8_t buffer[], int index){
  int value = buffer[index];
  return value > 128 ? value - 256 : value;
}

void loadMovementRules() {
  ResHandle handle = resource_get_handle(RESOURCE_ID_MOVEMENT_RULES);
  size_t size = resource_size(handle);
  uint8_t buffer[size];
  resource_load(handle, buffer, size);
  
  int x, y, dx, dy;  
  int levelIndex, creepIndex, ruleIndex, bufferIndex = 0;

  game.levelCount = buffer[bufferIndex++];
  game.levels = malloc(sizeof(Level) * game.levelCount);
  
  for(levelIndex = 0; levelIndex < game.levelCount; levelIndex++){
    Level level;
    level.creepCount = buffer[bufferIndex++];
    level.creeps = malloc(sizeof(Creep) * level.creepCount);
    for(creepIndex = 0; creepIndex < level.creepCount; creepIndex++){
      Creep creep;
      creep.currentRule = 0;
      creep.traveled = 0;
      x = buffer[bufferIndex++];
      y = buffer[bufferIndex++];
      creep.fullHealth = buffer[bufferIndex++];
      creep.health = creep.fullHealth;
      creep.type = buffer[bufferIndex++];
      creep.initialPosition = GPoint(x, y);
      creep.bounds = GRect(x, y, creepBitmap[creep.type]->bounds.size.w, creepBitmap[creep.type]->bounds.size.h);
      creep.ruleCount = buffer[bufferIndex++];
      creep.rules = malloc(sizeof(MovementRule) * creep.ruleCount);
      for(ruleIndex = 0; ruleIndex < creep.ruleCount; ruleIndex++){
        MovementRule rule;
        dx = getBufferInt(buffer, bufferIndex++);
        dy = getBufferInt(buffer, bufferIndex++);
        rule.delta = GPoint(dx, dy);
        rule.conditionType = buffer[bufferIndex++];
        rule.distance = buffer[bufferIndex++];
        creep.rules[ruleIndex] = rule;
      }
      level.creeps[creepIndex] = creep;
    }
    game.levels[levelIndex] = level; 
  }
  // i love you honey
}

void handle_init(void) {
  
  game.state = TipState;
  game.currentLevel = 0;

  player.armor = INITIAL_SHIP_ARMOR;
  player.fullArmor = INITIAL_SHIP_ARMOR;
  player.money = INITIAL_MONEY;

  // Init Window
  window = window_create();
  window_stack_push(window, true);

  // Init Canvas Layer
  windowLayer = window_get_root_layer(window);
  windowBounds = layer_get_frame(windowLayer);
  layer = layer_create(windowBounds);
  layer_set_update_proc(layer, layer_update_callback);
  layer_add_child(windowLayer, layer);

  // Load resources
  ship = gbitmap_create_with_resource(RESOURCE_ID_PLAYER_SHIP_IMAGE);
  creepBitmap[0] = gbitmap_create_with_resource(RESOURCE_ID_CREEP_1_IMAGE);
  creepBitmap[1] = gbitmap_create_with_resource(RESOURCE_ID_CREEP_2_IMAGE);
  creepBitmap[2] = gbitmap_create_with_resource(RESOURCE_ID_CREEP_3_IMAGE);
  creepBitmap[3] = gbitmap_create_with_resource(RESOURCE_ID_CREEP_4_IMAGE);
  tipBitmap = gbitmap_create_with_resource(RESOURCE_ID_TIP_IMAGE);
  
  loadMovementRules();

  // Init walls
  rightWall = windowBounds.size.w - padding - ship->bounds.size.w;
  leftWall = padding + ship->bounds.size.w;
  topWall = 20;
  bottomWall = 50;
  bottom = windowBounds.size.h - ship->bounds.size.h - padding;

  app_log(APP_LOG_LEVEL_INFO, "main", 513, "Size (%d,%d) Left: %d, Right: %d", windowBounds.size.w, windowBounds.size.h, leftWall, rightWall);

  // Place ship in bottom center
  shipBounds = GRect(
    windowBounds.size.w / 2 - ship->bounds.size.w / 2,
    bottom,
    ship->bounds.size.w,
    ship->bounds.size.h
  );

  resetLevel();

  // Init accelerometer callback
  accel_data_service_subscribe(0, NULL);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);

  window_set_click_config_provider_with_context(window, config_provider,  (void*)window);

  timer = app_timer_register(ACCEL_STEP_MS, timer_callback, NULL);
}

void handle_deinit() {
  accel_data_service_unsubscribe();
  gbitmap_destroy(ship);
  for(int i = 0; i < 4; i++) gbitmap_destroy(creepBitmap[i]);
  layer_destroy(layer);
  window_destroy(window);
  int levelIndex, creepIndex;
  for(levelIndex = 0; levelIndex < game.levelCount; levelIndex++){
    for(creepIndex = 0; creepIndex < game.levels[levelIndex].creepCount; creepIndex++){
      free(game.levels[levelIndex].creeps[creepIndex].rules);
    }
    free(game.levels[levelIndex].creeps);
  }
  free(game.levels);
}

int main(void) {
  handle_init();
  app_event_loop();
  handle_deinit();
}