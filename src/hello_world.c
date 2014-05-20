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
#define FULL_SHIP_HEALTH 4
#define LEVEL 0
#define STORE 1
#define GET_READY 2 
#define GAME_OVER 3
#define INITIAL_READY_COUNT 3
#define SHIP_MOVEMENT_SPEED 2
#define SHIP_FIRE_TIME_LAG 8
#define BULLET_SPEED 2
#define BULLET_RADIUS 1
#define BULLET_WIDTH 2
#define MAX_BULLETS_ON_SCREEN 32
#define ACCEL_MID 24
#define INITIAL_SCORE 100

// https://cloudpebble.net/ide/project/57655
// https://developer.getpebble.com/2/distribute/publish-to-pebble-appstore.html
// http://www.ticalc.org/archives/files/fileinfo/148/14876.html

// MAX RAM ALLOWED = 24576 bytes

Window *window;
Layer *windowLayer;
Layer *layer;
GBitmap *ship;
GBitmap *creepBitmap;
GBitmap *shipWeakBitmap;
GBitmap *creepWeakBitmap;
AppTimer *timer;
GRect windowBounds;
GRect shipBounds;
GRect bulletBounds;
GRect creepBounds;
GRect creepGroupBounds;
GPoint bulletRenderPoint;
AccelData accelData;
int padding = 8;  
int possibleNextPosition;
int rightWall, leftWall;
int bottom;
int maxBulletsInRow;
int lastPlayerFireTime = 0;
int bulletRow;
int movement;
int currentBulletIndex;
int* bulletPostions;
int* bulletRows;
int creepRowCount = 2;
int creepColCount = 8;
int creepFullHealth = 2;
int* creepHealth;
bool creepGroupMovingLeft = true;
int creepScore = CREEP_INITIAL_SCORE;
int creepCount;
int creepsLeft;
int* creepBulletsX;
int* creepBulletsY;
int shipHealth = FULL_SHIP_HEALTH;
int level = 1;
int score = INITIAL_SCORE;
char scoreText[12];
char levelText[8];
int gameTime = 0;
int gameState = GET_READY;
char readyText[4];
int readyStepsLeft = STEPS_IN_SECOND;
int readyCount = INITIAL_READY_COUNT;
int storeSelectionCosts[4] = {100, 200, 300, 500};
int gunType = DEFAULT_GUN;
int gunPowerUp = false;
int storeSelection = 0;
bool isPaused = false;

int getNextBulletIndex(){
  int next = bulletPostions[currentBulletIndex] == windowBounds.size.h ? currentBulletIndex : -1;
  if(next != -1) currentBulletIndex = (currentBulletIndex + 1) % MAX_BULLETS_ON_SCREEN;
  return next;
}

int getShipBulletRow(){
  return (shipBounds.origin.x + ship->bounds.size.w / 2) / BULLET_WIDTH;
}

void updateShipPosition(){
  accel_service_peek(&accelData);
  if(ABS(accelData.x) > ACCEL_MID){
    movement = accelData.x < 0 ? -SHIP_MOVEMENT_SPEED : SHIP_MOVEMENT_SPEED;
    possibleNextPosition = shipBounds.origin.x + movement;
    shipBounds.origin.x = MAX(MIN(possibleNextPosition, rightWall), padding);  
  }
}

void updatePlayerBullets(){
  for(int i = 0; i < MAX_BULLETS_ON_SCREEN; i++){
    int pos = bulletPostions[i];
    if(pos < windowBounds.size.h){
      pos -= BULLET_SPEED;
      if(pos <= 0) pos = windowBounds.size.h;
      bulletPostions[i] = pos;
    }
  }
}

void fireAt(int row){
  int nextIndex = getNextBulletIndex();
  if(nextIndex != -1){
    bulletPostions[nextIndex] = bottom;
    bulletRows[nextIndex] = row;
  }
}

void firePlayerBullet(){
  int row = getShipBulletRow();
  bool isTriple = gunType == TRIPLE_GUN;
  bool isDouble = gunType == DOUBLE_GUN;
  bool isDefault = gunType == DEFAULT_GUN;
  if(isDefault || isTriple) fireAt(row);
  if(isDouble || isTriple){
    fireAt(row - 2);
    fireAt(row + 2);
  }
  lastPlayerFireTime = SHIP_FIRE_TIME_LAG;
}

bool playerGunReady(){
  return !lastPlayerFireTime--;
}

void drawPlayerBullets(GContext* ctx){
  for(int i = 0; i < MAX_BULLETS_ON_SCREEN; i++){
    if(bulletPostions[i] < windowBounds.size.h){
      bulletRenderPoint.x = BULLET_WIDTH * bulletRows[i];
      bulletRenderPoint.y = bulletPostions[i];
      graphics_fill_circle(ctx, bulletRenderPoint, BULLET_RADIUS);
    }
  }
}

void resetLevel(){
  creepsLeft = creepCount;
  for(int i = 0; i < creepCount; i++){
    creepHealth[i] = creepFullHealth;
    creepBulletsY[i] = -1;
    creepBulletsX[i] = 0;
  }
  for(int i = 0; i < MAX_BULLETS_ON_SCREEN; i++){
    bulletPostions[i] = windowBounds.size.h;
    bulletRows[i] = -1;
  }
}

void handleLevelWin(){
  level++;
  creepFullHealth++;
  creepScore++;
  storeSelection = 0;
  gameState = STORE;
}

bool checkForCreepHit(int creepIndex){
  for(int i = 0; i < MAX_BULLETS_ON_SCREEN; i++){
    if(bulletPostions[i] < windowBounds.size.h){
      bulletRenderPoint.x = BULLET_WIDTH * bulletRows[i];
      bulletRenderPoint.y = bulletPostions[i];
      if(grect_contains_point(&creepBounds, &bulletRenderPoint)){
        // Reset bullet on hit
        bulletPostions[i] = windowBounds.size.h;
        // Hurt creep
        creepHealth[creepIndex] -= (gunPowerUp ? 2 : 1);
        if(creepHealth[creepIndex] == 0){
          // Creep killed
          score += creepScore;
          // Did we win the level?
          if(--creepsLeft == 0) handleLevelWin();
        }
        return true;
      }
    }
  }
  return false;
}

bool creepShouldFire(int index){
  return creepBulletsY[index] == -1 && (rand() % 100) < 10;
}

void updateCreeps(){
  
  // Check if any creeps are hit
  int cy = 0, y = 0, cx, x, i;
  for(; y < creepRowCount; y++){
    cx = 0;
    for(x = 0; x < creepColCount; x++){ 
      i = creepColCount * y + x;
      if(creepHealth[i] > 0){
        creepBounds.origin.x = creepGroupBounds.origin.x + cx;
        creepBounds.origin.y = creepGroupBounds.origin.y + cy;
        checkForCreepHit(i);
        if(creepShouldFire(i)){
          creepBulletsX[i] = creepBounds.origin.x + creepBounds.size.w / 2;
          creepBulletsY[i] = creepBounds.origin.y + creepBounds.size.h;
        }
      }
      if(creepBulletsY[i] != -1){
        // Check if creep bullet hit our ship
        bulletRenderPoint.x = creepBulletsX[i];
        bulletRenderPoint.y = creepBulletsY[i];
        bool hit = grect_contains_point(&shipBounds, &bulletRenderPoint);
        if(hit && --shipHealth == 0) gameState = GAME_OVER;
        if(hit || ++creepBulletsY[i] == windowBounds.size.h) creepBulletsY[i] = -1;
      }
      cx += creepBounds.size.w + padding;
    }
    cy += creepBounds.size.h + padding;
  }

  // Move creep group
  if(gameTime % 2 == 0){
    if(creepGroupMovingLeft){
      if(creepGroupBounds.origin.x > leftWall){
        creepGroupBounds.origin.x--;
      }else{
        creepGroupMovingLeft = false;
      }
    }else{
      if(creepGroupBounds.origin.x + creepGroupBounds.size.w < rightWall){
        creepGroupBounds.origin.x++;
      }else{
        creepGroupMovingLeft = true;
      }
    }
  }

}

void drawCreeps(GContext* ctx){
  int cy = 0, y = 0, cx, x, i;
  for(; y < creepRowCount; y++){
    cx = 0;
    for(x = 0; x < creepColCount; x++){
      i = creepColCount * y + x;      
      // If still alive
      if(creepHealth[i] > 0){
        creepBounds.origin.x = creepGroupBounds.origin.x + cx;
        creepBounds.origin.y = creepGroupBounds.origin.y + cy;
        graphics_draw_bitmap_in_rect(ctx, creepHealth[i] == 1 ? creepWeakBitmap : creepBitmap, creepBounds);
        // Draw its bullet if there
        if(creepBulletsY[i] != -1){
          bulletRenderPoint.x = creepBulletsX[i];
          bulletRenderPoint.y = creepBulletsY[i];
          graphics_fill_circle(ctx, bulletRenderPoint, BULLET_RADIUS);
        }
      }
      cx += creepBounds.size.w + padding;
    }
    cy += creepBounds.size.h + padding;
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
  snprintf(scoreText, 12, "$%d", score);
  snprintf(levelText, 8, "Lvl %d", level);
  drawText(ctx, levelText, GRect(2, 2, 64, 8));
  drawText(ctx, scoreText, GRect(windowBounds.size.w - 32, 2, 32, 8));
  snprintf(levelText, 8, "Lvl %d", level);
  drawText(ctx, levelText, GRect(2, 2, 64, 8));
  if(gameState == GAME_OVER){
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

  drawText(ctx, "Extra Life", GRect(left, y, 64, lineHeight));
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

void updateGetReady(){
  if(--readyStepsLeft == 0){
    if(--readyCount == 0){
      gameState = LEVEL;
      readyCount = INITIAL_READY_COUNT;
    }
    readyStepsLeft = STEPS_IN_SECOND;
  }  
}

// Game loop
void timer_callback(void *data) {
  gameTime++;
  if(gameState == LEVEL && !isPaused){
    updateShipPosition();
    updatePlayerBullets();
    updateCreeps();
    if(playerGunReady()) firePlayerBullet();
  }
  if(gameState == GET_READY) updateGetReady();
  // Redraw
  layer_mark_dirty(layer);
  // Ask for another loop
  timer = app_timer_register(ACCEL_STEP_MS, timer_callback, NULL);
}

// Draw
void layer_update_callback(Layer *me, GContext* ctx) {
  graphics_context_set_text_color(ctx, GColorBlack);
  drawScoreAndLevel(ctx);
  if(gameState == LEVEL || gameState == GET_READY){
    graphics_draw_bitmap_in_rect(ctx, shipHealth == 1 ? shipWeakBitmap : ship, shipBounds);
    drawPlayerBullets(ctx);
    drawCreeps(ctx);
  }
  if(gameState == GET_READY) drawGetReady(ctx);
  if(gameState == STORE) drawStore(ctx);
}

void select_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  if(gameState == GAME_OVER){
    score = INITIAL_SCORE;
    level = 1;
    shipHealth = FULL_SHIP_HEALTH;
    gameState = GET_READY;
    gunType = DEFAULT_GUN;
    gunPowerUp = false;
    creepScore = CREEP_INITIAL_SCORE;
    resetLevel();
  }
  if(gameState == LEVEL){
    isPaused = !isPaused;
  }
  if(gameState == STORE){
    if(storeSelection == DONE_SELECTION){
      gameState = GET_READY;
      resetLevel();
    }else{
      if(score >= storeSelectionCosts[storeSelection]){
        // Make purchase
        score -= storeSelectionCosts[storeSelection];
        switch(storeSelection){
          case EXTRA_LIFE_SELECTION:
            shipHealth++;
            break;
          case POWER_UP_SELECTION:
            gunPowerUp = true;
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
  storeSelection = MAX(storeSelection - 1, 0);
}

void down_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  storeSelection = MIN(storeSelection + 1, 4);
}

void config_provider(void *context) {
  window_set_click_context(BUTTON_ID_SELECT, context);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_single_click_handler);
  window_set_click_context(BUTTON_ID_UP, context);
  window_single_click_subscribe(BUTTON_ID_UP, up_single_click_handler);
  window_set_click_context(BUTTON_ID_DOWN, context);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_single_click_handler); 
}

void handle_init(void) {
  
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
  ship = gbitmap_create_with_resource(RESOURCE_ID_SHIP_IMAGE);
  creepBitmap = gbitmap_create_with_resource(RESOURCE_ID_CREEP_IMAGE);
  shipWeakBitmap = gbitmap_create_with_resource(RESOURCE_ID_SHIP_WEAK_IMAGE);
  creepWeakBitmap = gbitmap_create_with_resource(RESOURCE_ID_CREEP_WEAK_IMAGE);
  // Init walls
  rightWall = windowBounds.size.w - padding - ship->bounds.size.w;
  leftWall = padding + ship->bounds.size.w;
  bottom = windowBounds.size.h - ship->bounds.size.h - padding;

  // Place ship in bottom center
  shipBounds = GRect(
    windowBounds.size.w / 2 - ship->bounds.size.w / 2,
    bottom,
    ship->bounds.size.w,
    ship->bounds.size.h
  );

  // Init creep positions
  creepBounds = GRect(0, 0, creepBitmap->bounds.size.w, creepBitmap->bounds.size.h);
  creepGroupBounds = GRect(
    0,
    16,
    creepBounds.size.w * creepColCount + padding * (creepColCount - 1),
    creepBounds.size.h * creepRowCount + padding * (creepRowCount - 1)
  );
  creepGroupBounds.origin.x = windowBounds.size.w / 2 - creepGroupBounds.size.w / 2;

  creepCount = creepRowCount * creepColCount;
  int n = creepCount * sizeof(int);
  creepHealth = malloc(n);
  creepBulletsX = malloc(n);
  creepBulletsY = malloc(n);
  
  // Init bullets
  maxBulletsInRow = (windowBounds.size.w - padding * 2) / BULLET_WIDTH;
  
  n = MAX_BULLETS_ON_SCREEN * sizeof(int);
  bulletPostions = malloc(n);
  bulletRows = malloc(n);
  
  bulletRenderPoint = GPoint(0, 0);

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
  gbitmap_destroy(creepBitmap);
  gbitmap_destroy(shipWeakBitmap);
  gbitmap_destroy(creepWeakBitmap);
  layer_destroy(layer);
  window_destroy(window);
  free(bulletPostions);
  free(bulletRows);
  free(creepHealth);
  free(creepBulletsX);
  free(creepBulletsY);
}

int main(void) {
  handle_init();
  app_event_loop();
  handle_deinit();
}