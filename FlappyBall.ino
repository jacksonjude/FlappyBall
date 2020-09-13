/*
  FlappyBall (aka FloatyBall) for the Arduboy
  Written by Chris Martinez, 3/5/2014
  Modified by Scott Allen, April 2016
*/

/*
------------------------------------------------------------------------------
Original work copyright (c) 2014, Chris Martinez
Modifications copyright (c) 2016, 2017, Scott Allen
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holders nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
------------------------------------------------------------------------------
*/

/*
Save File fork by jacksonjude
copyright (c) 2020
*/

#include <Arduboy2.h>
#include <ArduboyTones.h>
#include "bitmaps.h"

#include <SPI.h>
#include <EEPROM.h>

Arduboy2 arduboy;
ArduboyTones sound(arduboy.audio.enabled);

// Things that make the game work the way it does
#define FRAMES_PER_SECOND 30   // The update and refresh speed
#define FRAC_BITS 6            // The number of bits in the fraction part of a fixed point int

// The following values define how fast the ball will accelerate and how high it will jump.
// They are given as fixed point integers so the true value is multiplied by (1 << FRAC_BITS)
// to give the value used. The resulting values must be integers.
#define BALL_ACCELERATION 16      // (0.25) the ball acceleration in pixels per frame squared
#define BALL_JUMP_VELOCITY (-144) // (-2.25) The inital velocity of a ball jump in pixels per frame
// ---------------------------

// This value is an offset to make it easier to work with negative numbers.
// It must be greater than the maximum number of pixels that Floaty will jump above
// the start height (based on the acceleration and initial velocity values),
// but must be low enough not to cause an overflow when added to the maximum
// screen height as an integer.
#define NEG_OFFSET 64

// Pipe
#define PIPE_ARRAY_SIZE 4  // At current settings only 3 sets of pipes can be onscreen at once
#define PIPE_MOVE_DISTANCE 2   // How far each pipe moves per frame
#define PIPE_GAP_MAX 30        // Maximum pipe gap
#define PIPE_GAP_MIN 18        // Minimum pipe gap
#define PIPE_GAP_REDUCE 5      // Number of points scored to reduce gap size
#define PIPE_WIDTH 12
#define PIPE_CAP_WIDTH 2
#define PIPE_CAP_HEIGHT 3      // Caps push back into the pipe, it's not added length
#define PIPE_MIN_HEIGHT 6      // Higher values center the gaps more
#define PIPE_GEN_FRAMES 32     // How many frames until a new pipe is generated

// Ball
#define BALL_RADIUS 4
#define BALL_Y_START ((HEIGHT / 2) - 1) // The height Floaty begins at
#define BALL_X 32              // Floaty's X Axis

// Storage Vars
byte gameState = 0;
unsigned int gameScore = 0;
unsigned int gameHighScore = 0;
char pipes[2][PIPE_ARRAY_SIZE]; // Row 0 for x values, row 1 for gap location
byte pipeGap = PIPE_GAP_MAX;    // Height of gap between pipes to fly through
byte pipeReduceCount = PIPE_GAP_REDUCE; // Score tracker for pipe gap reduction
char ballY = BALL_Y_START;      // Floaty's height
char ballYprev = BALL_Y_START;  // Previous height
char ballYi = BALL_Y_START;     // Floaty's initial height for the current arc
int ballV = 0;                  // For height calculations (Vi + ((a * t) / 2))
byte ballFrame = 0;             // Frame count for the current arc
char ballFlapper = BALL_RADIUS; // Floaty's wing length
char gameScoreX = 0;
char gameScoreY = 0;
byte gameScoreRiser = 0;
int rightButtonLoopCount = 0;
char unitName[ARDUBOY_UNIT_NAME_LEN + 1];
boolean inLoadScreen = false;

// Sounds
const uint16_t bing[] PROGMEM = {
  NOTE_FS1,107, NOTE_C4,500, TONES_END
};

const uint16_t point[] PROGMEM = {
  NOTE_B5,75, NOTE_E6,225, TONES_END
};

const uint16_t horns[] PROGMEM = {
  NOTE_C4,300, NOTE_D3,150, NOTE_REST,150, NOTE_C3,150, NOTE_G3,750, NOTE_D4,900,
  NOTE_B3,37, NOTE_G3,37, NOTE_E3,37, NOTE_C3,37, NOTE_A2,37, NOTE_F2,37,
  NOTE_D2,37, NOTE_B1,500, TONES_END
};

const uint16_t hit[] PROGMEM = {
  NOTE_C4,31, NOTE_CS4,31, NOTE_D4,31, TONES_END
};

// EEPROM Locations
#define EEPROM_DATA_SETUP_ADDRESS 499
#define CURRENT_HIGH_SCORE_ADDRESS 500
#define SKETCH_AUDIO_ENABLED_ADDRESS 501
#define LOADED_FILE_NUMBER_ADDRESS 502
#define FILE_1_SCORE_ADDRESS 503
#define FILE_2_SCORE_ADDRESS 504
#define FILE_3_SCORE_ADDRESS 505

void setup() {
  Serial.begin(9600);
  arduboy.begin();
  arduboy.setFrameRate(FRAMES_PER_SECOND);

  loadFileHighScore();

  if (EEPROM.read(EEPROM_DATA_SETUP_ADDRESS) == 1) {          //If the data setup is done already
    gameHighScore = EEPROM.read(CURRENT_HIGH_SCORE_ADDRESS);   //Read the game high score from data

    Serial.println("High Score Loaded: " + String(EEPROM.read(CURRENT_HIGH_SCORE_ADDRESS)));
    Serial.println("Audio Loaded: " + String(getSketchAudioEnabled()));

    if (arduboy.audio.enabled())
    {
      arduboy.audio.on();
    }

    if (getSketchAudioEnabled()) {      // If sound is enabled in the data
      Serial.println("Audio ON");
    } else {
      sound.noTone();                 // Stop anything that's playing
    	Serial.println("Audio OFF");
    }
  } else {
    EEPROM.write(EEPROM_DATA_SETUP_ADDRESS, 1);				  //Write default settings
    EEPROM.write(CURRENT_HIGH_SCORE_ADDRESS, 0);
    EEPROM.write(SKETCH_AUDIO_ENABLED_ADDRESS, 1);
    EEPROM.write(LOADED_FILE_NUMBER_ADDRESS, 1);
    EEPROM.write(FILE_1_SCORE_ADDRESS, 0);
    EEPROM.write(FILE_2_SCORE_ADDRESS, 0);
    EEPROM.write(FILE_3_SCORE_ADDRESS, 0);
  }

  delay(1000);
  arduboy.clear();
  arduboy.drawSlowXYBitmap(0,0,floatyball,128,64,1);
  arduboy.display();
  delay(500);
  arduboy.setCursor(18,55);
  arduboy.print("Press Any Button");
  arduboy.display();

  while (!arduboy.buttonsState()) { } // Wait for any key press
  debounceButtons();

  arduboy.initRandomSeed();
  for (byte x = 0; x < PIPE_ARRAY_SIZE; x++) { pipes[0][x] = 0; }  // Set all pipes offscreen
}

void loop() {
  if (!arduboy.nextFrame())
    return;

  if (arduboy.pressed(LEFT_BUTTON)) { // If the button for sound toggle is pressed
    boolean audioEnabled = !(getSketchAudioEnabled());
    setSketchAudioEnabled(audioEnabled);

    if (!audioEnabled) {              // If sound is enabled
      sound.noTone();                 // Stop anything that's playing
    } else {
      playTones(bing);                // Play a sound to indicate sound has been turned on
    }

    Serial.println("Audio Enabled: " + String(audioEnabled));

    debounceButtons();                // Wait for button release
  }

  arduboy.clear();

  // ===== State: Wait to begin =====
  if (gameState == 0) {     // If waiting to begin
    drawFloor();
    drawFloaty();
    drawInfo();             // Display usage info

    if (jumpPressed()) {    // Wait for a jump button press
      gameState = 1;        // Then start the game
      sound.noTone();       // Stop any playing sound
      beginJump();          // And make Floaty jump
    }

    if (arduboy.pressed(B_BUTTON))
    {
    	drawLoadScreen();
    }

    if (arduboy.pressed(RIGHT_BUTTON))
    {
    	rightButtonLoopCount++;
    	Serial.println(rightButtonLoopCount);
    	if (rightButtonLoopCount > 150)
    	{
    		debounceButtons();
    		refreshEraseScreen();
			  int choiceSelected = 2;
			  arduboy.drawSlowXYBitmap(6,(choiceSelected*10) + 7,arrow,8,6,1);

			  arduboy.display();

  			while (!arduboy.pressed(RIGHT_BUTTON))
  			{
  				if (arduboy.pressed(DOWN_BUTTON) || arduboy.pressed(UP_BUTTON))
  				{
  					refreshEraseScreen();

  					choiceSelected++;
  					if (choiceSelected > 2)
  					{
  						choiceSelected = 1;
  					}

  					arduboy.drawSlowXYBitmap(6,(choiceSelected*10) + 7,arrow,8,6,1);

  					arduboy.display();
  					debounceButtons();
  				}
  			}

  			if (choiceSelected == 1)
  			{
  				resetCurrentFile();
  			}

  			rightButtonLoopCount = 0;
  			arduboy.clear();

  			drawFloor();
    		drawFloaty();
    		drawInfo();
    	}
    }
    else
    {
    	rightButtonLoopCount = 0;
    }
  }

  // ===== State: Playing =====
  if (gameState == 1) {     // If the game is playing
    // If the ball isn't already rising, check for jump
    if ((ballYprev <= ballY) && jumpPressed()) {
      beginJump();          // Jump
    }

    moveFloaty();

    if (ballY < BALL_RADIUS) {  // If Floaty has moved above the top of the screen
      ballY = BALL_RADIUS;      // Set position to top
      startFalling();           // Start falling
    }

    if (arduboy.everyXFrames(PIPE_GEN_FRAMES)) { // Every PIPE_GEN_FRAMES worth of frames
      generatePipe();                  // Generate a pipe
    }

    for (byte x = 0; x < PIPE_ARRAY_SIZE; x++) {  // For each pipe array element
      if (pipes[0][x] != 0) {           // If the pipe is active
        pipes[0][x] = pipes[0][x] - PIPE_MOVE_DISTANCE;  // Then move it left
        if (pipes[0][x] + PIPE_WIDTH < 0) {  // If the pipe's right edge is off screen
          pipes[0][x] = 0;              // Then set it inactive
        }
        if (pipes[0][x] + PIPE_WIDTH == (BALL_X - BALL_RADIUS)) {  // If the pipe passed Floaty
          gameScore++;                  // Increment the score
          pipeReduceCount--;            // Decrement the gap reduction counter
          gameScoreX = BALL_X;                  // Load up the floating text with
          gameScoreY = ballY - BALL_RADIUS - 8; //  current ball x/y values
          gameScoreRiser = 15;          // And set it for 15 frames
          playTones(point);
        }
      }
    }

    if (gameScoreRiser > 0) {  // If we have floating text
      gameScoreY--;
      if (gameScoreY >= 0) { // If the score will still be on the screen
        arduboy.setCursor(gameScoreX, gameScoreY);
        arduboy.print(gameScore);
        gameScoreX = gameScoreX - 2;
        gameScoreRiser--;
      } else {
        gameScoreRiser = 0;
      }
    }

    if (ballY + BALL_RADIUS > (HEIGHT-1)) {  // If the ball has fallen below the screen
      ballY = (HEIGHT-1) - BALL_RADIUS;      // Don't let the ball go under :O
      gameState = 2;                        // Game over. State is 2.
    }
    // Collision checking
    for (byte x = 0; x < PIPE_ARRAY_SIZE; x++) { // For each pipe array element
      if (pipes[0][x] != 0) {                 // If the pipe is active (not 0)
        if (checkPipe(x)) { gameState = 2; }  // If the check is true, game over
      }
    }

    drawPipes();
    drawFloor();
    drawFloaty();

    // Reduce pipe gaps as the game progresses
    if ((pipeGap > PIPE_GAP_MIN) && (pipeReduceCount <= 0)) {
      pipeGap--;
      pipeReduceCount = PIPE_GAP_REDUCE;  // Restart the countdown
    }

    if (arduboy.pressed(RIGHT_BUTTON))
    {
    	arduboy.drawRect(56,28,16,16, WHITE);
    	arduboy.fillRect(57,29,14,14, BLACK);

    	arduboy.fillRect(61,32,2,8, WHITE);
    	arduboy.fillRect(65,32,2,8, WHITE);

    	arduboy.display();

  		debounceButtons();
  		while (!arduboy.pressed(RIGHT_BUTTON)) { }
  		debounceButtons();
  	}

  	arduboy.setCursor(0, 0);
  	arduboy.print(gameScore);
  }

  // ===== State: Game Over =====
  if (gameState == 2) {  // If the gameState is 2 then we draw a Game Over screen w/ score
    if (gameScore > gameHighScore) { gameHighScore = gameScore; }
    arduboy.display();              // Make sure final frame is drawn
    playTones(hit);                 // Hit sound
    delay(100);                     // Pause for the sound
    startFalling();                 // Start falling from current position
    while (ballY + BALL_RADIUS < (HEIGHT-1)) {  // While floaty is still airborne
      moveFloaty();
      arduboy.clear();
      drawPipes();
      drawFloor();
      drawFloaty();
      arduboy.display();
      while (!arduboy.nextFrame()) { }  // Wait for next frame
    }
    playTones(horns);                    // Sound the loser's horn
    arduboy.drawRect(16,8,96,48, WHITE); // Box border
    arduboy.fillRect(17,9,94,46, BLACK); // Black out the inside
    arduboy.drawSlowXYBitmap(30,12,gameover,72,14,1);
    arduboy.setCursor(56 - getOffset(gameScore),30);
    arduboy.print(gameScore);
    arduboy.setCursor(69,30);
    arduboy.print("Score");

    arduboy.setCursor(56 - getOffset(gameHighScore),42);
    arduboy.print(gameHighScore);
    arduboy.setCursor(69,42);
    arduboy.print("High");

    EEPROM.write(CURRENT_HIGH_SCORE_ADDRESS, gameHighScore);
    int fileNumber = EEPROM.read(LOADED_FILE_NUMBER_ADDRESS);
	  Serial.println(LOADED_FILE_NUMBER_ADDRESS + fileNumber);
	  EEPROM.write(LOADED_FILE_NUMBER_ADDRESS + fileNumber, gameHighScore);

    arduboy.display();
    delay(500);         // Give some time to stop pressing buttons

    while (!jumpPressed()) { } // Wait for a jump button to be pressed
    debounceButtons();

    gameState = 0;       // Then start the game paused
    gameScore = 0;       // Reset score to 0
    gameScoreRiser = 0;  // Clear the floating score
    for (byte x = 0; x < PIPE_ARRAY_SIZE; x++) { pipes[0][x] = 0; }  // set all pipes inactive
    ballY = BALL_Y_START;   // Reset to initial ball height
    pipeGap = PIPE_GAP_MAX; // Reset the pipe gap height
    pipeReduceCount = PIPE_GAP_REDUCE; // Init the pipe gap reduction counter
  }

  arduboy.display();  // Finally draw this thang
}

void drawInfo() {
  byte ulStart;     // Start of underline to indicate sound status
  byte ulLength;    // Length of underline

  arduboy.setCursor(6, 3);
  arduboy.print("A, Up: Jump");
  arduboy.setCursor(6, 51);
  arduboy.print("Left: Sound On/Off");
  arduboy.setCursor(6, 13);
  arduboy.print("Right: Pause/Select");
  arduboy.setCursor(6, 41);
  arduboy.print("B: Load File");

  if (getSketchAudioEnabled()) {
    ulStart = 13 * 6;
    ulLength = 2 * 6 - 1;
  } else {
    ulStart = 16 * 6;
    ulLength = 3 * 6 - 1;
  }
  arduboy.drawFastHLine(ulStart, 51 + 8, ulLength, WHITE); // Underline the current sound mode
  arduboy.drawFastHLine(ulStart, 51 + 9, ulLength, WHITE);
}

void drawFloor() {
  arduboy.drawFastHLine(0, HEIGHT-1, WIDTH, WHITE);
}

void drawFloaty() {
  ballFlapper--;
  if (ballFlapper < 0) { ballFlapper = BALL_RADIUS; }  // Flapper starts at the top of the ball
  arduboy.drawCircle(BALL_X, ballY, BALL_RADIUS, BLACK);  // Black out behind the ball
  arduboy.drawCircle(BALL_X, ballY, BALL_RADIUS, WHITE);  // Draw outline
  arduboy.drawLine(BALL_X, ballY, BALL_X - (BALL_RADIUS+1), ballY - ballFlapper, WHITE);  // Draw wing
  arduboy.drawPixel(BALL_X - (BALL_RADIUS+1), ballY - ballFlapper + 1, WHITE);  // Dot the wing
  arduboy.drawPixel(BALL_X + 1, ballY - 2, WHITE);  // Eye
}

void drawPipes() {
  for (byte x = 0; x < PIPE_ARRAY_SIZE; x++){
    if (pipes[0][x] != 0) {    // Value set to 0 if array element is inactive,
                               // otherwise it is the xvalue of the pipe's left edge
      // Pipes
      arduboy.drawRect(pipes[0][x], -1, PIPE_WIDTH, pipes[1][x], WHITE);
      arduboy.drawRect(pipes[0][x], pipes[1][x] + pipeGap, PIPE_WIDTH, HEIGHT - pipes[1][x] - pipeGap, WHITE);
      // Caps
      arduboy.drawRect(pipes[0][x] - PIPE_CAP_WIDTH, pipes[1][x] - PIPE_CAP_HEIGHT, PIPE_WIDTH + (PIPE_CAP_WIDTH*2), PIPE_CAP_HEIGHT, WHITE);
      arduboy.drawRect(pipes[0][x] - PIPE_CAP_WIDTH, pipes[1][x] + pipeGap, PIPE_WIDTH + (PIPE_CAP_WIDTH*2), PIPE_CAP_HEIGHT, WHITE);
      // Detail lines
      arduboy.drawLine(pipes[0][x]+2, 0, pipes[0][x]+2, pipes[1][x]-5, WHITE);
      arduboy.drawLine(pipes[0][x]+2, pipes[1][x] + pipeGap + 5, pipes[0][x]+2, HEIGHT - 3,WHITE);
    }
  }
}

void generatePipe() {
  for (byte x = 0; x < PIPE_ARRAY_SIZE; x++) {
    if (pipes[0][x] == 0) { // If the element is inactive
      pipes[0][x] = WIDTH;  // Then create it starting right of the screen
      pipes[1][x] = random(PIPE_MIN_HEIGHT, HEIGHT - PIPE_MIN_HEIGHT - pipeGap);
      return;
    }
  }
}

boolean checkPipe(byte x) {  // Collision detection, x is pipe to check
  byte AxA = BALL_X - (BALL_RADIUS-1);  // Hit box for floaty is a square
  byte AxB = BALL_X + (BALL_RADIUS-1);  // If the ball radius increases too much, corners
  byte AyA = ballY - (BALL_RADIUS-1);   // of the hitbox will go outside of floaty's
  byte AyB = ballY + (BALL_RADIUS-1);   // drawing
  byte BxA, BxB, ByA, ByB;

  // check top cylinder
  BxA = pipes[0][x];
  BxB = pipes[0][x] + PIPE_WIDTH;
  ByA = 0;
  ByB = pipes[1][x];
  if (AxA < BxB && AxB > BxA && AyA < ByB && AyB > ByA) { return true; } // Collided with top pipe

  // check top cap
  BxA = pipes[0][x] - PIPE_CAP_WIDTH;
  BxB = BxA + PIPE_WIDTH + (PIPE_CAP_WIDTH*2);
  ByA = pipes[1][x] - PIPE_CAP_HEIGHT;
  if (AxA < BxB && AxB > BxA && AyA < ByB && AyB > ByA) { return true; } // Collided with top cap

  // check bottom cylinder
  BxA = pipes[0][x];
  BxB = pipes[0][x] + PIPE_WIDTH;
  ByA = pipes[1][x] + pipeGap;
  ByB = HEIGHT-1;
  if (AxA < BxB && AxB > BxA && AyA < ByB && AyB > ByA) { return true; } // Collided with bottom pipe

  // check bottom cap
  BxA = pipes[0][x] - PIPE_CAP_WIDTH;
  BxB = BxA + PIPE_WIDTH + (PIPE_CAP_WIDTH*2);
  ByB = ByA + PIPE_CAP_HEIGHT;
  if (AxA < BxB && AxB > BxA && AyA < ByB && AyB > ByA) { return true; } // Collided with bottom pipe

  return false; // Nothing hits
}

boolean jumpPressed() { // Return "true" if a jump button is pressed
  return (arduboy.buttonsState() & (UP_BUTTON | A_BUTTON)) != 0;
}

void beginJump() {
  if (!sound.playing()) {   // Play "flap" sound only if nothing is playing
   playTone(NOTE_C1, 125);
  }
  ballV = BALL_JUMP_VELOCITY;
  ballFrame = 0;
  ballYi = ballY;
}

void startFalling() {   // Start falling from current height
  ballFrame = 0;        // Start a new fall
  ballYi = ballY;       // Set initial arc position
  ballV = 0;            // Set velocity to 0
}

void moveFloaty() {
  ballYprev = ballY;                   // Save the previous height
  ballFrame++;                         // Next frame
  ballV += (BALL_ACCELERATION / 2);    // Increase the velocity
                                     // Calculate Floaty's new height:
  ballY = ((((ballV * ballFrame)       // Complete "distance from initial height" calculation
           + (NEG_OFFSET << FRAC_BITS) // Add an offset to make sure the value is positive
           + (1 << (FRAC_BITS - 1)))   // Add 0.5 to round up
            >> FRAC_BITS)              // shift down to remove the fraction part
             - NEG_OFFSET)             // Remove previously added offset
              + ballYi;                // Add the result to the start height to get the new height
}

void debounceButtons() { // prevent "noisy" buttons from appearing as multiple presses
  delay(100);
  while (arduboy.buttonsState()) { }  // Wait for all keys released
  delay(100);
}

byte getOffset(byte s) {
  if (s > 9999) { return 20; }
  if (s > 999) { return 15; }
  if (s > 99) { return 10; }
  if (s > 9) { return 5; }
  return 0;
}

void playTones(uint16_t tones[])
{
  if (!arduboy.audio.enabled() || !getSketchAudioEnabled()) { return; }

  sound.tones(tones);
}

void playTone(uint16_t note, int length)
{
  if (!arduboy.audio.enabled() || !getSketchAudioEnabled()) { return; }

  sound.tone(NOTE_C1, length);
}

boolean getSketchAudioEnabled()
{
  return EEPROM.read(SKETCH_AUDIO_ENABLED_ADDRESS) == 1;
}

void setSketchAudioEnabled(boolean audioEnabled)
{
  EEPROM.write(SKETCH_AUDIO_ENABLED_ADDRESS, audioEnabled);
}

void drawLoadScreen()
{
  inLoadScreen = true;
	refreshLoadScreen();
	arduboy.drawSlowXYBitmap(6,(EEPROM.read(LOADED_FILE_NUMBER_ADDRESS)*10) + 7,arrow,8,6,1);
	arduboy.display();

	debounceButtons();

	int fileSelected = EEPROM.read(LOADED_FILE_NUMBER_ADDRESS);
	while (!arduboy.pressed(RIGHT_BUTTON))
	{
		if (arduboy.pressed(DOWN_BUTTON))
		{
			refreshLoadScreen();
			fileSelected++;
			if (fileSelected > 3)
			{
				fileSelected = 1;
			}

			arduboy.drawSlowXYBitmap(6,(fileSelected*10) + 7,arrow,8,6,1);

			arduboy.display();
			debounceButtons();
		}

    if (arduboy.pressed(UP_BUTTON))
    {
      refreshLoadScreen();
      fileSelected--;
      if (fileSelected < 1)
      {
        fileSelected = 3;
      }

      arduboy.drawSlowXYBitmap(6,(fileSelected*10) + 7,arrow,8,6,1);

      arduboy.display();
      debounceButtons();
    }

    if (arduboy.pressed(LEFT_BUTTON))
    {
      drawNameSelectionScreen(fileSelected);
    }
	}

	EEPROM.write(LOADED_FILE_NUMBER_ADDRESS, fileSelected);
	loadFileHighScore();

  inLoadScreen = false;

	debounceButtons();
}

void refreshLoadScreen()
{
	arduboy.clear();

	int file1 = EEPROM.read(FILE_1_SCORE_ADDRESS);
	int file2 = EEPROM.read(FILE_2_SCORE_ADDRESS);
	int file3 = EEPROM.read(FILE_3_SCORE_ADDRESS);

	arduboy.setCursor(6, 3);
	arduboy.print("High Scores");
	arduboy.drawFastHLine(5, 11, 67, WHITE);

  printFile(false, 0, 0, 1);
  printFile(true, 506, 510, 2);
  printFile(true, 511, 515, 3);
}

void printFile(boolean memory, int adressOn, int adressEnd, int file)
{
  arduboy.setCursor(16, file*10+6);
  arduboy.print(String(file) + ":" + String(EEPROM.read(CURRENT_HIGH_SCORE_ADDRESS+file+2)));

  String fileName;
  if (memory == true)
  {
    while (adressOn < adressEnd+1)
    {
      if (EEPROM.read(adressOn) != 0)
      {
        fileName = fileName + String((char)EEPROM.read(adressOn));
      }
      adressOn++;
    }
  }
  else
  {
    arduboy.readUnitName(unitName);
    fileName = unitName;
  }

  arduboy.setCursor(64, file*10+6);
  arduboy.print((String)fileName);
}

void loadFileHighScore()
{
	EEPROM.write(CURRENT_HIGH_SCORE_ADDRESS, EEPROM.read(LOADED_FILE_NUMBER_ADDRESS + EEPROM.read(LOADED_FILE_NUMBER_ADDRESS)));
	gameHighScore = EEPROM.read(LOADED_FILE_NUMBER_ADDRESS + EEPROM.read(LOADED_FILE_NUMBER_ADDRESS));
}

void resetCurrentFile()
{
	gameHighScore = 0;
	EEPROM.write(CURRENT_HIGH_SCORE_ADDRESS, 0);
	EEPROM.write(LOADED_FILE_NUMBER_ADDRESS + EEPROM.read(LOADED_FILE_NUMBER_ADDRESS), 0);
}

void refreshEraseScreen()
{
	arduboy.clear();

	arduboy.setCursor(6, 3);
  arduboy.print("Erase File?");
  arduboy.drawFastHLine(5, 11, 67, WHITE);

  arduboy.setCursor(16, 16);
  arduboy.print("Yes");
  arduboy.setCursor(16, 26);
  arduboy.print("No");

  arduboy.display();
}

void drawNameSelectionScreen(int fileSelected)
{
  const char characters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int selectedCharacter = -1;
  int charOn = 0;
  const int characterArrayLength = 26;
  String selectedCharacters = "";
  boolean rapidScroll = false;

  refreshNameSelectionScreen(charOn, selectedCharacter, characters);

  debounceButtons();
  while(!arduboy.pressed(RIGHT_BUTTON) && !arduboy.pressed(LEFT_BUTTON))
  {
    if (arduboy.pressed(DOWN_BUTTON))
    {
      selectedCharacter--;
      if (selectedCharacter < -1)
      {
        selectedCharacter = characterArrayLength-1;
      }

      arduboy.drawChar(23*charOn + 7, 0, characters[selectedCharacter], WHITE, 0, 4);
      debounceButtons();
    }

    if (arduboy.pressed(UP_BUTTON))
    {
      selectedCharacter++;
      if (selectedCharacter > characterArrayLength-1)
      {
        selectedCharacter = -1;
      }
      arduboy.drawChar(23*charOn + 7, 0, characters[selectedCharacter], WHITE, 0, 4);
      debounceButtons();
    }

    if (arduboy.pressed(B_BUTTON))
    {
      charOn++;
      if (charOn > 4)
      {
        charOn = 4;
      }
      debounceButtons();
      selectedCharacters = selectedCharacters + characters[selectedCharacter];
      arduboy.drawChar(23*charOn + 7, 0, characters[selectedCharacter], WHITE, 0, 4);
    }

    if (selectedCharacter < 0)
    {
      arduboy.drawChar(23*charOn + 7, 0, char(32), WHITE, 0, 4);
    }

    arduboy.display();
  }

  if (arduboy.pressed(RIGHT_BUTTON))
  {
    if (fileSelected == 2)
    {
      writeCharacters(selectedCharacters, 506, 510);
    }

    if (fileSelected == 3)
    {
      writeCharacters(selectedCharacters, 511, 515);
    }
  }

  debounceButtons();
  refreshLoadScreen();
  arduboy.drawSlowXYBitmap(6,(fileSelected*10) + 7,arrow,8,6,1);
  arduboy.display();
}

void refreshNameSelectionScreen(int charOn, int selectedCharacter, char characters[])
{
  arduboy.clear();
  arduboy.drawFastHLine(5, 32, 22, WHITE);
  arduboy.drawFastHLine(29, 32, 22, WHITE);
  arduboy.drawFastHLine(53, 32, 22, WHITE);
  arduboy.drawFastHLine(77, 32, 22, WHITE);
  arduboy.drawFastHLine(101, 32, 22, WHITE);

  arduboy.drawFastHLine(5, 33, 22, WHITE);
  arduboy.drawFastHLine(29, 33, 22, WHITE);
  arduboy.drawFastHLine(53, 33, 22, WHITE);
  arduboy.drawFastHLine(77, 33, 22, WHITE);
  arduboy.drawFastHLine(101, 33, 22, WHITE);

  arduboy.setCursor(5, 40);
  arduboy.print("B: Next Character");
  arduboy.setCursor(5, 50);
  arduboy.print("Right: Save Name");
  arduboy.setCursor(5, 60);
  arduboy.print("Left: Cancel");

  arduboy.drawChar(23*charOn + 7, 0, characters[selectedCharacter], WHITE, 0, 4);
}

void writeCharacters(String charactersToWrite, int adressStart, int adressEnd)
{
  int adressOn = adressStart;
  while (adressOn < adressEnd+1)
  {
    EEPROM.write(adressOn, charactersToWrite.charAt(adressOn-adressStart));
    Serial.println((String)charactersToWrite.charAt(adressOn-adressStart) + ":" + (String)adressOn);
    adressOn++;
  }
}
