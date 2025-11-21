// Terminal Puyo
// Jude Rorie

#include <ncursesw\ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <windows.h>

#define WIDTH 10			// playfield width (columns)
#define HEIGHT 20			// playfield height (rows)
#define SIZE 3				// 3x3 piece matrix (rotation center at [1,1])

// Block data
typedef struct {
	int shape[SIZE][SIZE];	// 1 = filled cell, 0 = empty
	int color[SIZE][SIZE];	// color index for each cell
} Block;

// Board occupancy and color grids
int board[HEIGHT][WIDTH];			// 1 if occupied, 0 otherwise
int board_color[HEIGHT][WIDTH];		// color index for occupied cells

// Current and next pieces + current piece coords
Block current;						// currently falling piece
Block next;							// next-piece preview
int cx = WIDTH / 2 - 1, cy = 0;		// current piece top-left (in 3x3 local coords)

// Game stats / difficulty
int score = 0;						// player's score
int level = 1;						// current level
int clears = 0;						// number of group clears (groups cleared)
int max_colors = 4;					// how many colors are available (difficulty)
double base_speed = 1.0;			// base fall interval (seconds) for difficulty
int input_locked = 0;				// when 1, ignore movement input

// Chain visual fade state
double fade_timer = 0.0;			// fade timer [0..1], >0 means show chain text
int last_chain = 0;					// last chain size for display

// Function declarations
int isCorner(int y, int x);
void makeBlock(Block *b);
void drawNextBlock();
void rotateRight(Block *b);
void rotateLeft(Block *b);
int checkCollision(Block *b, int nx, int ny);
int attemptRotation(Block rotated, int *nx, int *ny);
void placeBlock(Block *b, int bx, int by);
void drawGhost(Block *b, int x, int y);
int gravityFailSafe();
void animateGravity(int delay_us);
void gravity();
int dfs(int y, int x, int color, int coords[][2], int *count);
int clearGroups(double chain_mult);
void drawBoard(int chain, double fade);
void hardDrop();
void chooseDifficulty();
void lock_and_cascade();

/**
 * Determines whether the given coordinates represent a corner cell
 * in the 3x3 local piece matrix.
 *
 * @param y Row index in the 3x3 matrix.
 * @param x Column index in the 3x3 matrix.
 * @return 1 if (y, x) is a corner cell, 0 otherwise.
 */
int isCorner(int y, int x) {
	return ((y == 0 && x == 0) || (y == 0 && x == 2) || (y == 2 && x == 0) || (y == 2 && x == 2));
}

/**
 * Creates a new vertical 1x2 puyo piece (facing upward by default)
 * with random colors in the available color range.
 *
 * @param b Pointer to the Block structure to initialize.
 * @return void
 */
void makeBlock(Block *b) {
	memset(b->shape, 0, sizeof(b->shape));
	memset(b->color, 0, sizeof(b->color));
	// Fill middle column top and middle (vertical 1x2)
	b->shape[0][1] = 1;
	b->shape[1][1] = 1;
	// Assign random colors from available set
	b->color[0][1] = 1 + rand() % max_colors;
	b->color[1][1] = 1 + rand() % max_colors;
}

/**
 * Draws the "next piece" preview to the right of the playfield.
 *
 * @return void
 */
void drawNextBlock() {
	int offset = WIDTH * 2 + 8;
	mvprintw(3, offset, "Next:");
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			if (next.shape[y][x]) {
				attron(COLOR_PAIR(next.color[y][x]));
				mvaddch(4 + y, offset + x * 2, ' ' | A_REVERSE);
				mvaddch(4 + y, offset + x * 2 + 1, ' ' | A_REVERSE);
				attroff(COLOR_PAIR(next.color[y][x]));
			} else {
				mvaddch(4 + y, offset + x * 2, ' ');
				mvaddch(4 + y, offset + x * 2 + 1, ' ');
			}
		}
	}
}

/**
 * Rotates a block clockwise (right rotation) in its 3x3 matrix,
 * ignoring the corner cells.
 *
 * @param b Pointer to the Block structure to rotate.
 * @return void
 */
void rotateRight(Block *b) {
	int t[SIZE][SIZE], c[SIZE][SIZE];
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			if (!isCorner(y, x)) {
				t[y][x] = b->shape[SIZE - 1 - x][y];
				c[y][x] = b->color[SIZE - 1 - x][y];
			} else {
				t[y][x] = 0;
				c[y][x] = 0;
			}
		}
	}
	memcpy(b->shape, t, sizeof(t));
	memcpy(b->color, c, sizeof(c));
}

/**
 * Rotates a block counter-clockwise (left rotation) in its 3x3 matrix,
 * ignoring the corner cells.
 *
 * @param b Pointer to the Block structure to rotate.
 * @return void
 */
void rotateLeft(Block *b) {
	int t[SIZE][SIZE], c[SIZE][SIZE];
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			if (!isCorner(y, x)) {
				t[y][x] = b->shape[x][SIZE - 1 - y];
				c[y][x] = b->color[x][SIZE - 1 - y];
			} else {
				t[y][x] = 0;
				c[y][x] = 0;
			}
		}
	}
	memcpy(b->shape, t, sizeof(t));
	memcpy(b->color, c, sizeof(c));
}

/**
 * Checks for collision when placing a block with its 3x3 top-left
 * positioned at (nx, ny) on the playfield.
 *
 * @param b  Pointer to the Block being tested.
 * @param nx X-coordinate for the block's 3x3 top-left on the board.
 * @param ny Y-coordinate for the block's 3x3 top-left on the board.
 * @return 1 if a collision or out-of-bounds is detected, 0 otherwise.
 */
int checkCollision(Block *b, int nx, int ny) {
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			if (b->shape[y][x]) {
				int gx = nx + x;
				int gy = ny + y;
				// out-of-bounds or occupied
				if (gx < 0 || gx >= WIDTH || gy >= HEIGHT) return 1;
				if (gy >= 0 && board[gy][gx]) return 1;
			}
		}
	}
	return 0;
}

/**
 * Attempts to apply a rotation (already computed in `rotated`) with basic
 * wall and floor kicks, adjusting the piece position as needed.
 *
 * @param rotated A copy of the rotated Block to test.
 * @param nx      Pointer to the current X-coordinate; updated on success.
 * @param ny      Pointer to the current Y-coordinate; updated on success.
 * @return 1 if the rotation succeeds and `current` is updated, 0 otherwise.
 */
int attemptRotation(Block rotated, int *nx, int *ny) {
	// offsets to try: no offset, left, right, up, up-left, up-right
	int offsets[][2] = { {0,0}, {-1,0}, {1,0}, {0,-1}, {-1,-1}, {1,-1} };
	for (int i = 0; i < 6; i++) {
		int tx = *nx + offsets[i][0];
		int ty = *ny + offsets[i][1];
		if (!checkCollision(&rotated, tx, ty)) {
			*nx = tx;
			*ny = ty;
			current = rotated;
			return 1;
		}
	}
	return 0;
}

/**
 * Places a block permanently onto the board grid at the specified
 * 3x3 top-left board coordinates.
 *
 * @param b  Pointer to the Block to place.
 * @param bx X-coordinate of the block's 3x3 top-left on the board.
 * @param by Y-coordinate of the block's 3x3 top-left on the board.
 * @return void
 */
void placeBlock(Block *b, int bx, int by) {
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			if (b->shape[y][x]) {
				int gx = bx + x;
				int gy = by + y;
				if (gy >= 0 && gy < HEIGHT && gx >= 0 && gx < WIDTH) {
					board[gy][gx] = 1;
					board_color[gy][gx] = b->color[y][x];
				}
			}
		}
	}
}

/**
 * Draws a "ghost" outline of where the current piece would land if it
 * were hard-dropped from its current position.
 *
 * @param b Pointer to the Block to visualize.
 * @param x Current X-coordinate of the block's 3x3 top-left.
 * @param y Current Y-coordinate of the block's 3x3 top-left.
 * @return void
 */
void drawGhost(Block *b, int x, int y) {
	for (int xx = 0; xx < SIZE; xx++) {
		for (int yy = 0; yy < SIZE; yy++) {
			if (b->shape[yy][xx]) {
				int gx = x + xx;
				int gy = y + yy;
				
				// Drop each cell separately to its lowest possible position
				while (gy + 1 < HEIGHT && !board[gy + 1][gx]) {
					gy++;
				}
				
				if (gy >= 0 && gy < HEIGHT && gx >= 0 && gx < WIDTH) {
					mvaddch(gy + 1, (gx + 1) * 2, '.');
					mvaddch(gy + 1, (gx + 1) * 2 + 1, '.');
				}
			}
		}
	}
}

/**
 * Performs a single gravity step using a temporary buffer to avoid
 * mid-step corruption and moves each block as far down as possible.
 *
 * @return 1 if any block moved during this step, 0 otherwise.
 */
int gravityFailSafe() {
	int moved = 0;
	int tmp[HEIGHT][WIDTH] = {0};
	int tmpc[HEIGHT][WIDTH] = {0};
	// Copy each block to its lowest stable position in tmp buffers
	for (int y = HEIGHT - 1; y >= 0; y--) {
		for (int x = 0; x < WIDTH; x++) {
			if (board[y][x]) {
				int ny = y;
				while (ny + 1 < HEIGHT && !board[ny + 1][x]) ny++;
				if (ny != y) moved = 1;
				tmp[ny][x] = 1;
				tmpc[ny][x] = board_color[y][x];
			}
		}
	}
	// Commit tmp back to the real board
	memcpy(board, tmp, sizeof(board));
	memcpy(board_color, tmpc, sizeof(board_color));
	return moved;
}

/**
 * Applies animated gravity, repeatedly performing safe gravity steps
 * and redrawing the board with a delay between frames.
 *
 * @param delay_us Delay in microseconds between gravity frames.
 * @return void
 */
void animateGravity(int delay_us) {
	while (gravityFailSafe()) {
		drawBoard(last_chain, fade_timer);	// update display while visible gravity runs
		usleep(delay_us);
	}
}

/**
 * Applies gravity in a non-animated manner until all blocks are settled.
 *
 * @return void
 */
void gravity() {
	while (gravityFailSafe());
}

// Visited map used by DFS group detection
int visited[HEIGHT][WIDTH];

/**
 * Performs a depth-first search to collect all connected blocks of the
 * same color starting from (y, x), storing their coordinates.
 *
 * @param y      Starting row on the board.
 * @param x      Starting column on the board.
 * @param color  Target color to match.
 * @param coords Output array of (y, x) coordinate pairs for the group.
 * @param count  Pointer to an integer that accumulates the group size.
 * @return 1 if a new group exploration started from this call, 0 otherwise.
 */
int dfs(int y, int x, int color, int coords[][2], int *count) {
	if (y < 0 || y >= HEIGHT || x < 0 || x >= WIDTH) return 0;
	if (!board[y][x] || board_color[y][x] != color || visited[y][x]) return 0;
	visited[y][x] = 1;
	coords[*count][0] = y;
	coords[*count][1] = x;
	(*count)++;
	dfs(y + 1, x, color, coords, count);
	dfs(y - 1, x, color, coords, count);
	dfs(y, x + 1, color, coords, count);
	dfs(y, x - 1, color, coords, count);
	return 1;
}

/**
 * Finds and clears all color groups of size 4 or more, updating the
 * score and level based on the chain multiplier.
 *
 * @param chain_mult Multiplier applied to the score for this chain step.
 * @return Total number of blocks cleared during this pass.
 */
int clearGroups(double chain_mult) {
	memset(visited, 0, sizeof(visited));
	int total = 0, groups = 0;
	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDTH; x++) {
			if (board[y][x] && !visited[y][x]) {
				int coords[HEIGHT * WIDTH][2];
				int cnt = 0;
				int color = board_color[y][x];
				dfs(y, x, color, coords, &cnt);
				if (cnt >= 4) {
					// Remove only the matched group's cells
					for (int i = 0; i < cnt; i++) {
						int cy = coords[i][0], cx = coords[i][1];
						board[cy][cx] = 0;
						board_color[cy][cx] = 0;
					}
					score += (int)(cnt * 100 * chain_mult); // scoring per block * chain mult
					total += cnt;
					groups++;
				}
			}
		}
	}
	// Update clears and level if any groups removed
	if (groups > 0) {
		clears += groups;
		if (clears / 5 >= level) level++;
	}
	return total;
}

/**
 * Draws the playfield, including the settled board, the current piece,
 * the next-piece preview, UI elements, and any active chain fade text.
 *
 * @param chain Current chain count being displayed.
 * @param fade  Fade factor for the chain text (0.0–1.0).
 * @return void
 */
void drawBoard(int chain, double fade) {
	move(0, 0);

	// Draw top border
	mvprintw(0, 0, "+");
	for (int i = 0; i < WIDTH * 2 + 1; i++) printw("=");
	printw("+");

	// Draw sides and board
	for (int y = 0; y < HEIGHT; y++) {
		mvaddch(y + 1, 0, 'O');
		for (int x = 0; x < WIDTH; x++) {
			int screen_x = (x + 1) * 2; // shifted right one column
			if (board[y][x]) {
				attron(COLOR_PAIR(board_color[y][x]));
				mvaddch(y + 1, screen_x, ' ' | A_REVERSE);
				mvaddch(y + 1, screen_x + 1, ' ' | A_REVERSE);
				attroff(COLOR_PAIR(board_color[y][x]));
			} else {
				mvaddch(y + 1, screen_x, ' ');
				mvaddch(y + 1, screen_x + 1, ' ');
			}
		}
		mvaddch(y + 1, (WIDTH + 1) * 2, 'O'); // right wall
	}

	// Draw bottom border
	mvprintw(HEIGHT + 1, 0, "+");
	for (int i = 0; i < WIDTH * 2 + 1; i++) printw("=");
	printw("+");

	// Draw death spawn location
	mvprintw(1, 12, "X");
	mvprintw(2, 12, "X");
	mvprintw(1, 13, "X");
	mvprintw(2, 13, "X");
	
	// Draw ghost and current piece
	drawGhost(&current, cx, cy);
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			if (current.shape[y][x]) {
				int gx = cx + x;
				int gy = cy + y;
				if (gy >= 0 && gy < HEIGHT && gx >= 0 && gx < WIDTH) {
					attron(COLOR_PAIR(current.color[y][x]));
					mvaddch(gy + 1, (gx + 1) * 2, ' ' | A_REVERSE);
					mvaddch(gy + 1, (gx + 1) * 2 + 1, ' ' | A_REVERSE);
					attroff(COLOR_PAIR(current.color[y][x]));
				}
			}
		}
	}

	// Chain and UI
	if (fade_timer > 0.0 && last_chain > 1) {
		int attr = (fade > 0.5) ? A_BOLD : A_DIM;
		attron(attr);
		mvprintw(1, WIDTH * 2 + 8, "CHAIN x%d!", chain);
		attroff(attr);
	} else {
		mvprintw(1, WIDTH * 2 + 8, "             ");
	}

	// Next piece + info text
	drawNextBlock();
	mvprintw(HEIGHT + 3, 0, "Z/X: Rotate | Up: Hard Drop | Down: Soft Drop | Q: Quit");
	mvprintw(HEIGHT + 4, 0, "Score: %d  Level: %d  Clears: %d", score, level, clears);

	refresh();
}

/**
 * Instantly moves the current piece to its lowest valid position on
 * the board (a hard drop).
 *
 * @return void
 */
void hardDrop() {
	while (!checkCollision(&current, cx, cy + 1)) cy++;
}

/**
 * Presents a simple difficulty selection menu and configures the
 * number of colors and base fall speed accordingly.
 *
 * @return void
 */
void chooseDifficulty() {
	clear();
	echo();
	nodelay(stdscr, FALSE);
	mvprintw(4, 5, "Terminal Puyo");
	mvprintw(5, 5, "Jude Rorie");
	mvprintw(6, 5, "Select Difficulty:");
	mvprintw(7, 7, "1. Easy");
	mvprintw(8, 7, "2. Medium");
	mvprintw(9, 7, "3. Hard");
	mvprintw(10, 7, "4. Very Hard");
	mvprintw(12, 5, "Enter Choice (1-4): ");
	refresh();
	int choice = getch();
	noecho();
	nodelay(stdscr, TRUE);
	clear();
	switch (choice) {
		case '1': max_colors = 4; base_speed = 1.0; break;
		case '2': max_colors = 5; base_speed = 0.8; break;
		case '3': max_colors = 6; base_speed = 0.6; break;
		default: max_colors = 7; base_speed = 0.45; break;
	}
}

/**
 * Handles the piece lock-in and cascade logic after a piece can no
 * longer move: locking, clearing groups, applying gravity, spawning
 * the next piece, and checking for game over.
 *
 * @return void
 */
void lock_and_cascade() {
	// Disable movement
	input_locked = 1;
	
	// Lock current piece into board
	placeBlock(&current, cx, cy);

	// Spawn next piece
	current = next;
	makeBlock(&next);
	cx = WIDTH / 2 - 1;
	cy = 0;

	// Full cascade loop: clear → gravity → recheck until stable
	int chain = 0;
	while (1) {
		int any_wave_cleared = 0;

		while (1) {
			double mult = 1.0 + 0.5 * (chain);
			int cleared = clearGroups(mult);
			if (cleared == 0) break;

			any_wave_cleared = 1;
			chain++;
			last_chain = chain;
			fade_timer = 5.0;

			for (int f = 0; f < 4; f++) {
				drawBoard(last_chain, fade_timer * (1.0 - (double)f / 4.0));
				usleep(100000);
			}
			animateGravity(25000);
		}

		gravity();
		if (!any_wave_cleared) break;
	}

	// If no clears occurred, reset chain display
	if (chain == 0) {
		last_chain = 0;
		fade_timer = 0.0;
	}
	
	// Enable movement
	input_locked = 0;
	
	// Game Over check
	if (checkCollision(&current, cx, cy)) {
		mvprintw(HEIGHT / 2, WIDTH - 3, "GAME OVER!");
		mvprintw(HEIGHT / 2 + 2, WIDTH - 10, " Press any key to quit ");
		refresh();
		nodelay(stdscr, FALSE);
		getch();
		endwin();
		exit(0);
	}
}

/**
 * Entry point for the Terminal Puyo game. Initializes ncurses,
 * configures colors and difficulty, then runs the main game loop.
 *
 * @return Exit status code (0 on normal termination).
 */
int main(void) {
	srand(time(NULL));
	initscr();
	noecho();
	cbreak();
	curs_set(0);
	keypad(stdscr, TRUE);
	start_color();

	// Initialize colors
	for (int i = 1; i <= 7; i++)
		init_pair(i, i, COLOR_BLACK);

	chooseDifficulty();
	nodelay(stdscr, TRUE);
	makeBlock(&current);
	makeBlock(&next);

	struct timespec last_fall, now;
	clock_gettime(CLOCK_MONOTONIC, &last_fall);

	int running = 1, soft = 0;

	// Grab inputs and clock for realtime gameplay
	while (running) {
		drawBoard(last_chain, fade_timer);
		gravity();

		int ch = getch();

		// Input keys
		if (!input_locked) {
			if (ch == 'q') break;
			else if (ch == KEY_LEFT && !checkCollision(&current, cx - 1, cy)) cx--;
			else if (ch == KEY_RIGHT && !checkCollision(&current, cx + 1, cy)) cx++;
			else if (ch == 'z' || ch == 'Z') { Block r = current; rotateLeft(&r); attemptRotation(r, &cx, &cy); }
			else if (ch == 'x' || ch == 'X') { Block r = current; rotateRight(&r); attemptRotation(r, &cx, &cy); }
			else if (ch == KEY_DOWN) soft = 1;
			else if (ch == KEY_UP) {
				hardDrop();
				lock_and_cascade();
				gravity();
				clearGroups(1);
			}
			else soft = 0;
		}

		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = (now.tv_sec - last_fall.tv_sec) + (now.tv_nsec - last_fall.tv_nsec) / 1e9;
		double fall_time = (soft ? 0.025 : base_speed) / (0.5 + (level * 0.25));

		if (fade_timer > 0.0) {
			fade_timer -= 0.03;
			if (fade_timer < 0.0) fade_timer = 0.0;
		}

		if (elapsed >= fall_time) {
			last_fall = now;
			if (!checkCollision(&current, cx, cy + 1)) cy++;
			else {
				lock_and_cascade();
				gravity();
				clearGroups(1);
			}
		}
		usleep(10000);
	}
	endwin();
	return 0;
}
