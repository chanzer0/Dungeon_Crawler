#include <unistd.h>
#include <ncurses.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <boost/algorithm/string.hpp>

#include "io.h"
#include "move.h"
#include "path.h"
#include "pc.h"
#include "utils.h"
#include "dungeon.h"
#include "object.h"
#include "npc.h"

#define DIVIDER "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
#define DIVIDER_44 "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

/* Same ugly hack we did in path.c */
static dungeon *the_dungeon;

typedef struct io_message {
  /* Will print " --more-- " at end of line when another message follows. *
   * Leave 10 extra spaces for that.                                      */
  char msg[71];
  struct io_message *next;
} io_message_t;

static io_message_t *io_head, *io_tail;

void io_init_terminal(void)
{
  initscr();
  raw();
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);
  start_color();
  init_pair(COLOR_RED, COLOR_RED, COLOR_BLACK);
  init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
  init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLACK);
  init_pair(COLOR_BLUE, COLOR_BLUE, COLOR_BLACK);
  init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLACK);
  init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
}

static std::vector<std::string> split(const std::string& string, int n)
{
   /* Initialize variables */
   int32_t num_chars = 0;
   std::string str(string);
   std::vector<std::string> ret, words;
   std::string line;
 
   /* Replace newline with space */
   for(uint32_t index = 0; index < str.length(); index++){
     if (str[index] == '\n'){str[index] = ' ';}
   }
 
   /* Put string into vector of words */
   boost::split(words, str, boost::is_any_of(" "));
 
   /* Add words to ret while they obey the 'n' line length */
   for(uint32_t j = 0; j < words.size(); ){
     while (num_chars <= n && j < words.size()){
       num_chars += words[j].length() + 1;
       if (num_chars >= n){continue;}
       line += words[j] + " ";
       j++;
     }
     num_chars = 0;
     ret.push_back(line); 
     line = "";
   }
   return ret;
}

void io_reset_terminal(void)
{
  endwin();

  while (io_head) {
    io_tail = io_head;
    io_head = io_head->next;
    free(io_tail);
  }
  io_tail = NULL;
}

void io_queue_message(const char *format, ...)
{
  io_message_t *tmp;
  va_list ap;

  if (!(tmp = (io_message_t *) malloc(sizeof (*tmp)))) {
    perror("malloc");
    exit(1);
  }

  tmp->next = NULL;

  va_start(ap, format);

  vsnprintf(tmp->msg, sizeof (tmp->msg), format, ap);

  va_end(ap);

  if (!io_head) {
    io_head = io_tail = tmp;
  } else {
    io_tail->next = tmp;
    io_tail = tmp;
  }
}

static void io_print_message_queue(uint32_t y, uint32_t x)
{
  while (io_head) {
    io_tail = io_head;
    attron(COLOR_PAIR(COLOR_CYAN));
    mvprintw(y, x, "%-80s", io_head->msg);
    attroff(COLOR_PAIR(COLOR_CYAN));
    io_head = io_head->next;
    if (io_head) {
      attron(COLOR_PAIR(COLOR_CYAN));
      mvprintw(y, x + 70, "%10s", " --more-- ");
      attroff(COLOR_PAIR(COLOR_CYAN));
      refresh();
      getch();
    }
    free(io_tail);
  }
  io_tail = NULL;
}

void io_display_tunnel(dungeon *d)
{
  uint32_t y, x;
  clear();
  for (y = 0; y < DUNGEON_Y; y++) {
    for (x = 0; x < DUNGEON_X; x++) {
      if (charxy(x, y) == d->PC) {
        mvaddch(y + 1, x, charxy(x, y)->symbol);
      } else if (hardnessxy(x, y) == 255) {
        mvaddch(y + 1, x, '*');
      } else {
        mvaddch(y + 1, x, '0' + (d->pc_tunnel[y][x] % 10));
      }
    }
  }
  refresh();
}

void io_display_distance(dungeon *d)
{
  uint32_t y, x;
  clear();
  for (y = 0; y < DUNGEON_Y; y++) {
    for (x = 0; x < DUNGEON_X; x++) {
      if (charxy(x, y)) {
        mvaddch(y + 1, x, charxy(x, y)->symbol);
      } else if (hardnessxy(x, y) != 0) {
        mvaddch(y + 1, x, ' ');
      } else {
        mvaddch(y + 1, x, '0' + (d->pc_distance[y][x] % 10));
      }
    }
  }
  refresh();
}

static char hardness_to_char[] =
  "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

void io_display_hardness(dungeon *d)
{
  uint32_t y, x;
  clear();
  for (y = 0; y < DUNGEON_Y; y++) {
    for (x = 0; x < DUNGEON_X; x++) {
      /* Maximum hardness is 255.  We have 62 values to display it, but *
       * we only want one zero value, so we need to cover [1,255] with  *
       * 61 values, which gives us a divisor of 254 / 61 = 4.164.       *
       * Generally, we want to avoid floating point math, but this is   *
       * not gameplay, so we'll make an exception here to get maximal   *
       * hardness display resolution.                                   */
      mvaddch(y + 1, x, (d->hardness[y][x]                             ?
                         hardness_to_char[1 + (int) ((d->hardness[y][x] /
                                                      4.2))] : ' '));
    }
  }
  refresh();
}

static void io_redisplay_visible_monsters(dungeon *d, pair_t cursor)
{
  /* This was initially supposed to only redisplay visible monsters.  After *
   * implementing that (comparitivly simple) functionality and testing, I   *
   * discovered that it resulted to dead monsters being displayed beyond    *
   * their lifetimes.  So it became necessary to implement the function for *
   * everything in the light radius.  In hindsight, it would be better to   *
   * keep a static array of the things in the light radius, generated in    *
   * io_display() and referenced here to accelerate this.  The whole point  *
   * of this is to accelerate the rendering of multi-colored monsters, and  *
   * it is *significantly* faster than that (it eliminates flickering       *
   * artifacts), but it's still significantly slower than it could be.  I   *
   * will revisit this in the future to add the acceleration matrix.        */
  pair_t pos;
  uint32_t color;
  uint32_t illuminated;

  for (pos[dim_y] = -PC_VISUAL_RANGE;
       pos[dim_y] <= PC_VISUAL_RANGE;
       pos[dim_y]++) {
    for (pos[dim_x] = -PC_VISUAL_RANGE;
         pos[dim_x] <= PC_VISUAL_RANGE;
         pos[dim_x]++) {
      if ((d->PC->position[dim_y] + pos[dim_y] < 0) ||
          (d->PC->position[dim_y] + pos[dim_y] >= DUNGEON_Y) ||
          (d->PC->position[dim_x] + pos[dim_x] < 0) ||
          (d->PC->position[dim_x] + pos[dim_x] >= DUNGEON_X)) {
        continue;
      }
      if ((illuminated = is_illuminated(d->PC,
                                        d->PC->position[dim_y] + pos[dim_y],
                                        d->PC->position[dim_x] + pos[dim_x]))) {
        attron(A_BOLD);
      }
      if (cursor[dim_y] == d->PC->position[dim_y] + pos[dim_y] &&
          cursor[dim_x] == d->PC->position[dim_x] + pos[dim_x]) {
        mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                d->PC->position[dim_x] + pos[dim_x], '*');
      } else if (d->character_map[d->PC->position[dim_y] + pos[dim_y]]
                          [d->PC->position[dim_x] + pos[dim_x]] &&
          can_see(d, d->PC->position,
                  d->character_map[d->PC->position[dim_y] + pos[dim_y]]
                                  [d->PC->position[dim_x] +
                                   pos[dim_x]]->position, 1, 0)) {
        attron(COLOR_PAIR((color = d->character_map[d->PC->position[dim_y] +
                                                    pos[dim_y]]
                                                   [d->PC->position[dim_x] +
                                                    pos[dim_x]]->get_color())));
        mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                d->PC->position[dim_x] + pos[dim_x],
                character_get_symbol(d->character_map[d->PC->position[dim_y] +
                                                      pos[dim_y]]
                                                     [d->PC->position[dim_x] +
                                                      pos[dim_x]]));
        attroff(COLOR_PAIR(color));
      } else if (d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                          [d->PC->position[dim_x] + pos[dim_x]] &&
                 (can_see(d, d->PC->position,
                          d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                                   [d->PC->position[dim_x] +
                                    pos[dim_x]]->get_position(), 1, 0) ||
                 d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                          [d->PC->position[dim_x] + pos[dim_x]]->have_seen())) {
        attron(COLOR_PAIR(d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                                   [d->PC->position[dim_x] +
                                    pos[dim_x]]->get_color()));
        mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                d->PC->position[dim_x] + pos[dim_x],
                d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                         [d->PC->position[dim_x] + pos[dim_x]]->get_symbol());
        attroff(COLOR_PAIR(d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                                    [d->PC->position[dim_x] +
                                     pos[dim_x]]->get_color()));
      } else {
        switch (pc_learned_terrain(d->PC,
                                   d->PC->position[dim_y] + pos[dim_y],
                                   d->PC->position[dim_x] +
                                   pos[dim_x])) {
        case ter_wall:
        case ter_wall_immutable:
        case ter_unknown:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], ' ');
          break;
        case ter_floor:
        case ter_floor_room:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '.');
          break;
        case ter_floor_hall:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '#');
          break;
        case ter_debug:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '*');
          break;
        case ter_stairs_up:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '<');
          break;
        case ter_stairs_down:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '>');
          break;
        case ter_marketplace:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '+');
          break;
        default:
 /* Use zero as an error symbol, since it stands out somewhat, and it's *
  * not otherwise used.                                                 */
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '0');
        }
      }
      attroff(A_BOLD);
    }
  }

  refresh();
}

static int compare_monster_distance(const void *v1, const void *v2)
{
  const character *const *c1 = (const character *const *) v1;
  const character *const *c2 = (const character *const *) v2;

  return (the_dungeon->pc_distance[(*c1)->position[dim_y]]
                                  [(*c1)->position[dim_x]] -
          the_dungeon->pc_distance[(*c2)->position[dim_y]]
                                  [(*c2)->position[dim_x]]);
}

static character *io_nearest_visible_monster(dungeon *d)
{
  character **c, *n;
  uint32_t x, y, count, i;

  c = (character **) malloc(d->num_monsters * sizeof (*c));

  /* Get a linear list of monsters */
  for (count = 0, y = 1; y < DUNGEON_Y - 1; y++) {
    for (x = 1; x < DUNGEON_X - 1; x++) {
      if (d->character_map[y][x] && d->character_map[y][x] != d->PC) {
        c[count++] = d->character_map[y][x];
        assert(count <= d->num_monsters);
      }
    }
  }

  /* Sort it by distance from PC */
  the_dungeon = d;
  qsort(c, count, sizeof (*c), compare_monster_distance);

  for (n = NULL, i = 0; i < count; i++) {
    if (can_see(d, character_get_pos(d->PC), character_get_pos(c[i]), 1, 0)) {
      n = c[i];
      break;
    }
  }

  free(c);

  return n;
}

void io_display(dungeon *d)
{
  pair_t pos;
  uint32_t illuminated;
  uint32_t color;
  character *c;
  int32_t visible_monsters;

  clear();
  for (visible_monsters = -1, pos[dim_y] = 0;
       pos[dim_y] < DUNGEON_Y;
       pos[dim_y]++) {
    for (pos[dim_x] = 0; pos[dim_x] < DUNGEON_X; pos[dim_x]++) {
      if ((illuminated = is_illuminated(d->PC, pos[dim_y], pos[dim_x]))) {
        attron(A_BOLD);
      }
      if (d->character_map[pos[dim_y]][pos[dim_x]] &&
          can_see(d, character_get_pos(d->PC), 
                  character_get_pos(d->character_map[pos[dim_y]][pos[dim_x]]), 1, 0)) {

        visible_monsters++;
        attron(COLOR_PAIR((color = d->character_map[pos[dim_y]][pos[dim_x]]->get_color())));
        mvaddch(pos[dim_y] + 1, pos[dim_x],
                character_get_symbol(d->character_map[pos[dim_y]][pos[dim_x]]));
        attroff(COLOR_PAIR(color));
      } else if (d->objmap[pos[dim_y]][pos[dim_x]] &&
                 (d->objmap[pos[dim_y]][pos[dim_x]]->have_seen() ||
                  can_see(d, character_get_pos(d->PC), pos, 1, 0))) {
        attron(COLOR_PAIR(d->objmap[pos[dim_y]][pos[dim_x]]->get_color()));
        mvaddch(pos[dim_y] + 1, pos[dim_x],
                d->objmap[pos[dim_y]][pos[dim_x]]->get_symbol());
        attroff(COLOR_PAIR(d->objmap[pos[dim_y]][pos[dim_x]]->get_color()));
      } else {
        switch (pc_learned_terrain(d->PC,pos[dim_y], pos[dim_x])) {
        case ter_wall:
        case ter_wall_immutable:
        case ter_unknown:
          mvaddch(pos[dim_y] + 1, pos[dim_x], ' ');
          break;
        case ter_floor:
        case ter_floor_room:
          mvaddch(pos[dim_y] + 1, pos[dim_x], '.');
          break;
        case ter_floor_hall:
          mvaddch(pos[dim_y] + 1, pos[dim_x], '#');
          break;
        case ter_debug:
          mvaddch(pos[dim_y] + 1, pos[dim_x], '*');
          break;
        case ter_stairs_up:
          mvaddch(pos[dim_y] + 1, pos[dim_x], '<');
          break;
        case ter_stairs_down:
          mvaddch(pos[dim_y] + 1, pos[dim_x], '>');
          break;
        case ter_marketplace:
          mvaddch(pos[dim_y] + 1, pos[dim_x], '+');
          break;
        default:
 /* Use zero as an error symbol, since it stands out somewhat, and it's *
  * not otherwise used.                                                 */
          mvaddch(pos[dim_y] + 1, pos[dim_x], '0');
        }
      }
      if (illuminated) {
        attroff(A_BOLD);
      }
    }
  }

  mvprintw(23, 0, "PC position is (%3d,%2d).",
           character_get_x(d->PC), character_get_y(d->PC));

  mvprintw(22, 1, "%d known %s.", visible_monsters,
           !visible_monsters || visible_monsters > 1 ? "monsters" : "monster");
  if ((c = io_nearest_visible_monster(d))) {
    mvprintw(22, 30, "Nearest visible monster: %c at %d %c by %d %c.",
             c->symbol,
             abs(c->position[dim_y] - d->PC->position[dim_y]),
             ((c->position[dim_y] - d->PC->position[dim_y]) <= 0 ?
              'N' : 'S'),
             abs(c->position[dim_x] - d->PC->position[dim_x]),
             ((c->position[dim_x] - d->PC->position[dim_x]) <= 0 ?
              'E' : 'W'));
  } else {
    mvprintw(22, 30, "Nearest visible monster: NONE.");
  }

  io_print_message_queue(0, 0);

  refresh();
}

static void io_redisplay_non_terrain(dungeon *d, pair_t cursor)
{
  /* For the wiz-mode teleport, in order to see color-changing effects. */
  pair_t pos;
  uint32_t color;
  uint32_t illuminated;

  for (pos[dim_y] = 0; pos[dim_y] < DUNGEON_Y; pos[dim_y]++) {
    for (pos[dim_x] = 0; pos[dim_x] < DUNGEON_X; pos[dim_x]++) {
      if ((illuminated = is_illuminated(d->PC,
                                        pos[dim_y],
                                        pos[dim_x]))) {
        attron(A_BOLD);
      }
      if (cursor[dim_y] == pos[dim_y] && cursor[dim_x] == pos[dim_x]) {
        mvaddch(pos[dim_y] + 1, pos[dim_x], '*');
      } else if (d->character_map[pos[dim_y]][pos[dim_x]]) {
        attron(COLOR_PAIR((color = d->character_map[pos[dim_y]]
                                                   [pos[dim_x]]->get_color())));
        mvaddch(pos[dim_y] + 1, pos[dim_x],
                character_get_symbol(d->character_map[pos[dim_y]][pos[dim_x]]));
        attroff(COLOR_PAIR(color));
      } else if (d->objmap[pos[dim_y]][pos[dim_x]]) {
        attron(COLOR_PAIR(d->objmap[pos[dim_y]][pos[dim_x]]->get_color()));
        mvaddch(pos[dim_y] + 1, pos[dim_x],
                d->objmap[pos[dim_y]][pos[dim_x]]->get_symbol());
        attroff(COLOR_PAIR(d->objmap[pos[dim_y]][pos[dim_x]]->get_color()));
      }
      attroff(A_BOLD);
    }
  }

  refresh();
}

void io_display_no_fog(dungeon *d)
{
  uint32_t y, x;
  uint32_t color;
  character *c;

  clear();
  for (y = 0; y < DUNGEON_Y; y++) {
    for (x = 0; x < DUNGEON_X; x++) {
      if (d->character_map[y][x]) {
        attron(COLOR_PAIR((color = d->character_map[y][x]->get_color())));
        mvaddch(y + 1, x, character_get_symbol(d->character_map[y][x]));
        attroff(COLOR_PAIR(color));
      } else if (d->objmap[y][x]) {
        attron(COLOR_PAIR(d->objmap[y][x]->get_color()));
        mvaddch(y + 1, x, d->objmap[y][x]->get_symbol());
        attroff(COLOR_PAIR(d->objmap[y][x]->get_color()));
      } else {
        switch (mapxy(x, y)) {
        case ter_wall:
        case ter_wall_immutable:
          mvaddch(y + 1, x, ' ');
          break;
        case ter_floor:
        case ter_floor_room:
          mvaddch(y + 1, x, '.');
          break;
        case ter_floor_hall:
          mvaddch(y + 1, x, '#');
          break;
        case ter_debug:
          mvaddch(y + 1, x, '*');
          break;
        case ter_stairs_up:
          mvaddch(y + 1, x, '<');
          break;
        case ter_stairs_down:
          mvaddch(y + 1, x, '>');
          break;
        case ter_marketplace:
          mvaddch(y + 1, x, '+');
          break;
        default:
 /* Use zero as an error symbol, since it stands out somewhat, and it's *
  * not otherwise used.                                                 */
          mvaddch(y + 1, x, '0');
        }
      }
    }
  }

  mvprintw(23, 1, "PC position is (%2d,%2d).",
           d->PC->position[dim_x], d->PC->position[dim_y]);
  mvprintw(22, 1, "%d known %s.", d->num_monsters,
           !d->num_monsters || d->num_monsters > 1 ? "monsters" : "monster");
  if ((c = io_nearest_visible_monster(d))) {
    mvprintw(22, 30, "Nearest visible monster: %c at %d %c by %d %c.",
             c->symbol,
             abs(c->position[dim_y] - d->PC->position[dim_y]),
             ((c->position[dim_y] - d->PC->position[dim_y]) <= 0 ?
              'N' : 'S'),
             abs(c->position[dim_x] - d->PC->position[dim_x]),
             ((c->position[dim_x] - d->PC->position[dim_x]) <= 0 ?
              'E' : 'W'));
  } else {
    mvprintw(22, 30, "Nearest visible monster: NONE.");
  }

  io_print_message_queue(0, 0);
  refresh();
}

void io_display_monster_list(dungeon *d)
{
  mvprintw(11, 33, " HP:    XXXXX ");
  mvprintw(12, 33, " Speed: XXXXX ");
  mvprintw(14, 27, " Hit any key to continue. ");
  refresh();
  getch();
}

uint32_t io_teleport_pc(dungeon *d)
{
  pair_t dest;
  int c;
  fd_set readfs;
  struct timeval tv;

  io_display_no_fog(d);

  mvprintw(0, 0, "Choose a location.  't' to teleport to; 'r' for random.");

  dest[dim_y] = d->PC->position[dim_y];
  dest[dim_x] = d->PC->position[dim_x];

  mvaddch(dest[dim_y] + 1, dest[dim_x], '*');
  refresh();

  do {
    do{
      FD_ZERO(&readfs);
      FD_SET(STDIN_FILENO, &readfs);

      tv.tv_sec = 0;
      tv.tv_usec = 125000; /* An eigth of a second */

      io_redisplay_non_terrain(d, dest);
    } while (!select(STDIN_FILENO + 1, &readfs, NULL, NULL, &tv));
    /* Can simply draw the terrain when we move the cursor away, *
     * because if it is a character or object, the refresh       *
     * function will fix it for us.                              */
    switch (mappair(dest)) {
    case ter_wall:
    case ter_wall_immutable:
    case ter_unknown:
      mvaddch(dest[dim_y] + 1, dest[dim_x], ' ');
      break;
    case ter_floor:
    case ter_floor_room:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '.');
      break;
    case ter_floor_hall:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '#');
      break;
    case ter_debug:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '*');
      break;
    case ter_stairs_up:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '<');
      break;
    case ter_stairs_down:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '>');
      break;
    case ter_marketplace:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '+');
      break;
    default:
 /* Use zero as an error symbol, since it stands out somewhat, and it's *
  * not otherwise used.                                                 */
      mvaddch(dest[dim_y] + 1, dest[dim_x], '0');
    }
    switch ((c = getch())) {
    case '7':
    case 'y':
    case KEY_HOME:
      if (dest[dim_y] != 1) {
        dest[dim_y]--;
      }
      if (dest[dim_x] != 1) {
        dest[dim_x]--;
      }
      break;
    case '8':
    case 'k':
    case KEY_UP:
      if (dest[dim_y] != 1) {
        dest[dim_y]--;
      }
      break;
    case '9':
    case 'u':
    case KEY_PPAGE:
      if (dest[dim_y] != 1) {
        dest[dim_y]--;
      }
      if (dest[dim_x] != DUNGEON_X - 2) {
        dest[dim_x]++;
      }
      break;
    case '6':
    case 'l':
    case KEY_RIGHT:
      if (dest[dim_x] != DUNGEON_X - 2) {
        dest[dim_x]++;
      }
      break;
    case '3':
    case 'n':
    case KEY_NPAGE:
      if (dest[dim_y] != DUNGEON_Y - 2) {
        dest[dim_y]++;
      }
      if (dest[dim_x] != DUNGEON_X - 2) {
        dest[dim_x]++;
      }
      break;
    case '2':
    case 'j':
    case KEY_DOWN:
      if (dest[dim_y] != DUNGEON_Y - 2) {
        dest[dim_y]++;
      }
      break;
    case '1':
    case 'b':
    case KEY_END:
      if (dest[dim_y] != DUNGEON_Y - 2) {
        dest[dim_y]++;
      }
      if (dest[dim_x] != 1) {
        dest[dim_x]--;
      }
      break;
    case '4':
    case 'h':
    case KEY_LEFT:
      if (dest[dim_x] != 1) {
        dest[dim_x]--;
      }
      break;
    }
  } while (c != 't' && c != 'r');

  if (c == 'r') {
    do {
      dest[dim_x] = rand_range(1, DUNGEON_X - 2);
      dest[dim_y] = rand_range(1, DUNGEON_Y - 2);
    } while (charpair(dest) || mappair(dest) < ter_floor);
  }

  if (charpair(dest) && charpair(dest) != d->PC) {
    io_queue_message("Teleport failed.  Destination occupied.");
  } else {  
    d->character_map[d->PC->position[dim_y]][d->PC->position[dim_x]] = NULL;
    d->character_map[dest[dim_y]][dest[dim_x]] = d->PC;

    d->PC->position[dim_y] = dest[dim_y];
    d->PC->position[dim_x] = dest[dim_x];
  }

  pc_observe_terrain(d->PC, d);
  dijkstra(d);
  dijkstra_tunnel(d);

  io_display(d);

  return 0;
}

#if o
/* Adjectives to describe our monsters */
static const char *adjectives[] = {
  "A menacing ",
  "A threatening ",
  "A horrifying ",
  "An intimidating ",
  "An aggressive ",
  "A frightening ",
  "A terrifying ",
  "A terrorizing ",
  "An alarming ",
  "A frightening ",
  "A dangerous ",
  "A glowering ",
  "A glaring ",
  "A scowling ",
  "A chilling ",
  "A scary ",
  "A creepy ",
  "An eerie ",
  "A spooky ",
  "A slobbering ",
  "A drooling ",
  " A horrendous ",
  "An unnerving ",
  "A cute little ",  /* Even though they're trying to kill you, */
  "A teeny-weenie ", /* they can still be cute!                 */
  "A fuzzy ",
  "A fluffy white ",
  "A kawaii ",       /* For our otaku */
  "Hao ke ai de "    /* And for our Chinese */
  /* And there's one special case (see below) */
};
#endif

static void io_scroll_monster_list(char (*s)[40], uint32_t count)
{
  uint32_t offset;
  uint32_t i;

  offset = 0;

  while (1) {
    for (i = 0; i < 13; i++) {
      mvprintw(i + 6, 19, " %-40s ", s[i + offset]);
    }
    switch (getch()) {
    case KEY_UP:
      if (offset) {
        offset--;
      }
      break;
    case KEY_DOWN:
      if (offset < (count - 13)) {
        offset++;
      }
      break;
    case 27:
      return;
    }

  }
}

static bool is_vowel(const char c)
{
  return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
          c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U');
}

static void io_list_monsters_display(dungeon *d,
                                     character **c,
                                     uint32_t count)
{
  uint32_t i;
  char (*s)[40]; /* pointer to array of 40 char */

  s = (char (*)[40]) malloc((count + 1) * sizeof (*s));

  mvprintw(3, 19, " %-40s ", "");
  /* Borrow the first element of our array for this string: */
  snprintf(s[0], 40, "You know of %d monsters:", count);
  mvprintw(4, 19, " %-40s ", s);
  mvprintw(5, 19, " %-40s ", "");

  for (i = 0; i < count; i++) {
    snprintf(s[i], 40, "%3s%s (%c): %2d %s by %2d %s",
             (is_unique(c[i]) ? "" :
              (is_vowel(character_get_name(c[i])[0]) ? "An " : "A ")),
             character_get_name(c[i]),
             character_get_symbol(c[i]),
             abs(character_get_y(c[i]) - character_get_y(d->PC)),
             ((character_get_y(c[i]) - character_get_y(d->PC)) <= 0 ?
              "North" : "South"),
             abs(character_get_x(c[i]) - character_get_x(d->PC)),
             ((character_get_x(c[i]) - character_get_x(d->PC)) <= 0 ?
              "East" : "West"));
    if (count <= 13) {
      /* Handle the non-scrolling case right here. *
       * Scrolling in another function.            */
      mvprintw(i + 6, 19, " %-40s ", s[i]);
    }
  }

  if (count <= 13) {
    mvprintw(count + 6, 19, " %-40s ", "");
    mvprintw(count + 7, 19, " %-40s ", "Hit escape to continue.");
    while (getch() != 27 /* escape */)
      ;
  } else {
    mvprintw(19, 19, " %-40s ", "");
    mvprintw(20, 19, " %-40s ",
             "Arrows to scroll, escape to continue.");
    io_scroll_monster_list(s, count);
  }

  free(s);
}

static void io_list_monsters(dungeon *d)
{
  character **c;
  uint32_t x, y, count;

  c = (character **) malloc(d->num_monsters * sizeof (*c));

  /* Get a linear list of monsters */
  for (count = 0, y = 1; y < DUNGEON_Y - 1; y++) {
    for (x = 1; x < DUNGEON_X - 1; x++) {
      if (d->character_map[y][x] && d->character_map[y][x] != d->PC &&
          can_see(d, character_get_pos(d->PC),
                  character_get_pos(d->character_map[y][x]), 1, 0)) {
        c[count++] = d->character_map[y][x];
      }
    }
  }

  /* Sort it by distance from PC */
  the_dungeon = d;
  qsort(c, count, sizeof (*c), compare_monster_distance);

  /* Display it */
  io_list_monsters_display(d, c, count);
  free(c);

  /* And redraw the dungeon */
  io_display(d);
}

void io_display_ch(dungeon_t *d)
{
  mvprintw(11, 33, " HP:    %5d ", d->PC->hp);
  mvprintw(12, 33, " Speed: %5d ", d->PC->speed);
  mvprintw(14, 27, " Hit any key to continue. ");
  refresh();
  getch();
  io_display(d);
}

void io_object_to_string(object *o, char *s, uint32_t size, dungeon *d)
{
  if (o && o->get_type() != objtype_GOLD) {
    snprintf(s, size, "%s (sp: %d, dmg: %d+%dd%d)",
             o->get_name(), o->get_speed(), o->get_damage_base(),
             o->get_damage_number(), o->get_damage_sides());
  } else if (o && o->get_type() == objtype_GOLD){
    snprintf(s, size, "%s [x%d](sp: %d, dmg: %d+%dd%d)", 
             o->get_name(), d->PC->get_gold_count(), o->get_speed(), o->get_damage_base(),
             o->get_damage_number(), o->get_damage_sides());
  } else {
    *s = '\0';
  }
}

uint32_t io_wear_eq(dungeon_t *d)
{
  uint32_t i, key;
  char s[61];
  mvprintw(2, 10, " %-58s ", " ");
  mvprintw(3, 10, " %-58s ", "WEAR EQUIPMENT                            (ESC to cancel)");
  mvprintw(4, 10, " %-58s ", DIVIDER);

  for (i = 0; i < MAX_INVENTORY; i++) {
    /* We'll write 12 lines, 10 of inventory, 1 blank, and 1 prompt. *
     * We'll limit width to 60 characters, so very long object names *
     * will be truncated.  In an 80x24 terminal, this gives offsets  *
     * at 10 x and 6 y to start printing things.  Same principal in  *
     * other functions, below.                                       */
    io_object_to_string(d->PC->in[i], s, 61, d);
    if (d->PC->in[i]) { attron(COLOR_PAIR(d->PC->in[i]->get_color())); }
    mvprintw(i + 5, 10, " %c) %-55s ", '0' + i, s);
    if (d->PC->in[i]) { attroff(COLOR_PAIR(d->PC->in[i]->get_color())); }
    mvprintw(i + 5, 10, " %c)", '0' + i);
  } mvprintw(i + 5, 10, " %-58s ", DIVIDER);
  refresh();

  while (1) {
    if ((key = getch()) == 27 /* ESC */) {
      io_display(d);
      return 1;
    }

    if (key < '0' || key > '9') {
      if (isprint(key)) {
        snprintf(s, 61, "Invalid input: '%c'.  Enter 0-9 or ESC to cancel.",
                 key);
        mvprintw(16, 10, " %-58s ", s);
        mvprintw(17, 10, " %-58s ", DIVIDER);
      } else {
        mvprintw(16, 10, " %-58s ",
                 "Invalid input.  Enter 0-9 or ESC to cancel.");
        mvprintw(17, 10, " %-58s ", DIVIDER);
      }
      refresh();
      continue;
    }

    if (!d->PC->in[key - '0']) {
      mvprintw(16, 10, " %-58s ", "Empty inventory slot.  Try again.");
      mvprintw(17, 10, " %-58s ", DIVIDER);
      continue;
    }

    if (!d->PC->wear_in(key - '0')) {
      return 0;
    }
    snprintf(s, 61, "Can't wear %s.  Try again.",
             d->PC->in[key - '0']->get_name());
    mvprintw(16, 10, " %-58s ", s);
    mvprintw(17, 10, " %-58s ", DIVIDER);
    refresh();
  }

  return 1;
}

void io_display_in(dungeon_t *d)
{
  uint32_t i;
  char s[61];
  mvprintw(2, 10, " %-58s ", " ");
  mvprintw(3, 10, " %-58s ", "PC INVENTORY                    (Press any key to resume)");
  mvprintw(4, 10, " %-58s ", DIVIDER);
  for (i = 0; i < MAX_INVENTORY; i++) {
    io_object_to_string(d->PC->in[i], s, 61, d);
    if (d->PC->in[i]) { attron(COLOR_PAIR(d->PC->in[i]->get_color())); }
    mvprintw(i + 5, 10, " %c) %-55s ", '0' + i, s);
    if (d->PC->in[i]) { attroff(COLOR_PAIR(d->PC->in[i]->get_color())); }
    mvprintw(i + 5, 10, " %c)", '0' + i);
  }

  mvprintw(15, 10, " %-58s ", DIVIDER);

  refresh();

  getch();

  io_display(d);
}

uint32_t io_remove_eq(dungeon_t *d)
{
  uint32_t i, key;
  char s[61], t[61];
  mvprintw(2, 10, " %-58s ", " ");
  mvprintw(3, 10, " %-58s ", "TAKEOFF AN ITEM                 (Press ESC to resume)");
  mvprintw(4, 10, " %-58s ", DIVIDER);
  for (i = 0; i < num_eq_slots; i++) {
    sprintf(s, "[%s]", eq_slot_name[i]);
    io_object_to_string(d->PC->eq[i], t, 61, d);
    if (d->PC->eq[i]) { attron(COLOR_PAIR(d->PC->eq[i]->get_color())); }
    mvprintw(i + 5, 10, " %c %-9s) %-45s ", 'a' + i, s, t);
    if (d->PC->eq[i]) { attroff(COLOR_PAIR(d->PC->eq[i]->get_color())); }
    mvprintw(i + 5, 10, " %c %-9s)", 'a' + i, s);
  }
  mvprintw(17, 10, " %-58s ", DIVIDER);
  refresh();

  while (1) {
    if ((key = getch()) == 27 /* ESC */) {
      io_display(d);
      return 1;
    }

    if (key < 'a' || key > 'l') {
      if (isprint(key)) {
        snprintf(s, 61, "Invalid input: '%c'.  Enter a-l or ESC to cancel.",
                 key);
        mvprintw(18, 10, " %-58s ", s);
        mvprintw(19, 10, " %-58s ", DIVIDER);
      } else {
        mvprintw(18, 10, " %-58s ",
                 "Invalid input.  Enter a-l or ESC to cancel.");
        mvprintw(19, 10, " %-58s ", DIVIDER);
      }
      refresh();
      continue;
    }

    if (!d->PC->eq[key - 'a']) {
      mvprintw(18, 10, " %-58s ", "Empty equipment slot.  Try again.");
      mvprintw(19, 10, " %-58s ", DIVIDER);
      continue;
    }

    if (!d->PC->remove_eq(key - 'a')) {
      return 0;
    }

    snprintf(s, 61, "Can't take off %s.  Try again.",
             d->PC->eq[key - 'a']->get_name());
    mvprintw(18, 10, " %-58s ", s);
    mvprintw(19, 10, " %-58s ", DIVIDER);
  }

  return 1;
}

void io_display_eq(dungeon_t *d)
{
  uint32_t i;
  char s[61], t[61];

  mvprintw(2, 10, " %-58s ", " ");
  mvprintw(3, 10, " %-58s ", "PC EQUIPMENT                    (Press any key to resume)");
  mvprintw(4, 10, " %-58s ", DIVIDER);
  for (i = 0; i < num_eq_slots; i++) {
    sprintf(s, "[%s]", eq_slot_name[i]);
    io_object_to_string(d->PC->eq[i], t, 61, d);
    if (d->PC->eq[i]) { attron(COLOR_PAIR(d->PC->eq[i]->get_color())); }
    mvprintw(i + 5, 10, " %c %-9s) %-45s ", 'a' + i, s, t);
    if (d->PC->eq[i]) { attroff(COLOR_PAIR(d->PC->eq[i]->get_color())); }
    mvprintw(i + 5, 10, " %c %-9s)", 'a' + i, s);
  }
  mvprintw(17, 10, " %-58s ", DIVIDER);

  refresh();

  getch();

  io_display(d);
}

uint32_t io_drop_in(dungeon_t *d)
{
  uint32_t i, key;
  char s[61];
  mvprintw(2, 10, " %-58s ", " ");
  mvprintw(3, 10, " %-58s ", "DROP AN ITEM                    (Press any key to resume)");
  mvprintw(4, 10, " %-58s ", DIVIDER);
  for (i = 0; i < MAX_INVENTORY; i++) {
      if (d->PC->in[i]) { attron(COLOR_PAIR(d->PC->in[i]->get_color())); }
      mvprintw(i + 5, 10, " %c) %-55s ", '0' + i,
               d->PC->in[i] ? d->PC->in[i]->get_name() : "");
      if (d->PC->in[i]) { attroff(COLOR_PAIR(d->PC->in[i]->get_color())); }
  }
  mvprintw(15, 10, " %-58s ", DIVIDER);
  refresh();

  while (1) {
    if ((key = getch()) == 27 /* ESC */) {
      io_display(d);
      return 1;
    }

    if (key < '0' || key > '9') {
      if (isprint(key)) {
        snprintf(s, 61, "Invalid input: '%c'.  Enter 0-9 or ESC to cancel.",
                 key);
        mvprintw(16, 10, " %-58s ", s);
        mvprintw(17, 10, " %-58s ", DIVIDER);
      } else {
        mvprintw(16, 10, " %-58s ",
                 "Invalid input.  Enter 0-9 or ESC to cancel.");
        mvprintw(17, 10, " %-58s ", DIVIDER);
      }
      refresh();
      continue;
    }

    if (!d->PC->in[key - '0']) {
      mvprintw(16, 10, " %-58s ", "Empty inventory slot.  Try again.");
      mvprintw(17, 10, " %-58s ", DIVIDER);
      continue;
    }

    if (!d->PC->drop_in(d, key - '0')) {
      return 0;
    }

    snprintf(s, 61, "Can't drop %s.  Try again.",
             d->PC->in[key - '0']->get_name());
    mvprintw(16, 10, " %-58s ", s);
    mvprintw(17, 10, " %-58s ", DIVIDER);
    refresh();
  }

  return 1;
}

static uint32_t io_display_obj_info(object *o)
{
  uint32_t i;
  std::vector<std::string> obj_desc = split(o->get_description(), 45);
  mvprintw(3, 10, " %-44s ", "OBJECT DESCRIPTION (Press any key to resume)"); 
  mvprintw(4 , 10, " %-44s ", DIVIDER_44);
  attron(COLOR_PAIR(o->get_color()));
  mvprintw(5, 10, " %-44s ", o->get_name());
  attroff(COLOR_PAIR(o->get_color()));
  mvprintw(6, 10, " %-44s ", " ");
  for (i = 0; i < obj_desc.size(); i++){
    mvprintw(i + 7, 10, " %-44s ", obj_desc[i].c_str());
  } mvprintw(i + 7, 10, " %-44s ", DIVIDER_44);
  refresh();
  getch();
  return 0;
}

static uint32_t io_inspect_eq(dungeon_t *d);

static uint32_t io_inspect_in(dungeon_t *d)
{
  uint32_t i, key;
  char s[61];

  mvprintw(2, 10, " %-58s ", " ");
  mvprintw(3, 10, " %-58s ", "INSPECT ITEM [0..9] (ESC to cancel, '/' for equipment)"); 
  mvprintw(4 , 10, " %-58s ", DIVIDER);
  for (i = 0; i < MAX_INVENTORY; i++) {
    io_object_to_string(d->PC->in[i], s, 61, d);
    if (d->PC->in[i]){ attron(COLOR_PAIR(d->PC->in[i]->get_color())); }
    mvprintw(i + 5, 10, " %c) %-55s ", '0' + i,
             d->PC->in[i] ? d->PC->in[i]->get_name() : "");
    if (d->PC->in[i]){ attroff(COLOR_PAIR(d->PC->in[i]->get_color())); }
  }
  mvprintw(15, 10, " %-58s ", DIVIDER);
  refresh();

  while (1) {
    if ((key = getch()) == 27 /* ESC */) {
      io_display(d);
      return 1;
    }

    if (key == '/') {
      io_display(d);
      io_inspect_eq(d);
      return 1;
    }

    if (key < '0' || key > '9') {
      if (isprint(key)) {
        snprintf(s, 61, "Invalid input: '%c'.  Enter 0-9 or ESC to cancel.",
                 key);
        mvprintw(16, 10, " %-58s ", s);
        mvprintw(17, 10, " %-58s ", DIVIDER);
      } else {
        mvprintw(16, 10, " %-58s ",
                 "Invalid input.  Enter 0-9 or ESC to cancel.");
        mvprintw(17, 10, " %-58s ", DIVIDER);
      }
      refresh();
      continue;
    }

    if (!d->PC->in[key - '0']) {
      mvprintw(16, 10, " %-58s ", "Empty inventory slot.  Try again.");
      mvprintw(17, 10, " %-58s ", DIVIDER);
      refresh();
      continue;
    }

    io_display(d);
    io_display_obj_info(d->PC->in[key - '0']);
    io_display(d);
    return 1;
  }

  return 1;
}

static uint32_t io_inspect_monster(dungeon_t *d)
{
  pair_t dest, tmp;
  int c;
  uint32_t i;
  fd_set readfs;
  struct timeval tv;

  io_display(d);

  mvprintw(0, 0, "Choose a monster.  't' to select; 'ESC' to cancel.");

  dest[dim_y] = d->PC->position[dim_y];
  dest[dim_x] = d->PC->position[dim_x];

  mvaddch(dest[dim_y] + 1, dest[dim_x], '*');
  refresh();

  do {
    do{
      FD_ZERO(&readfs);
      FD_SET(STDIN_FILENO, &readfs);

      tv.tv_sec = 0;
      tv.tv_usec = 125000; /* An eigth of a second */

      io_redisplay_visible_monsters(d, dest);
    } while (!select(STDIN_FILENO + 1, &readfs, NULL, NULL, &tv));
    /* Can simply draw the terrain when we move the cursor away, *
     * because if it is a character or object, the refresh       *
     * function will fix it for us.                              */
    switch (mappair(dest)) {
    case ter_wall:
    case ter_wall_immutable:
    case ter_unknown:
      mvaddch(dest[dim_y] + 1, dest[dim_x], ' ');
      break;
    case ter_floor:
    case ter_floor_room:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '.');
      break;
    case ter_floor_hall:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '#');
      break;
    case ter_debug:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '*');
      break;
    case ter_stairs_up:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '<');
      break;
    case ter_stairs_down:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '>');
      break;
    case ter_marketplace:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '+');
      break;
    default:
 /* Use zero as an error symbol, since it stands out somewhat, and it's *
  * not otherwise used.                                                 */
      mvaddch(dest[dim_y] + 1, dest[dim_x], '0');
    }
    tmp[dim_y] = dest[dim_y];
    tmp[dim_x] = dest[dim_x];
    switch ((c = getch())) {
    case '7':
    case 'y':
    case KEY_HOME:
      tmp[dim_y]--;
      tmp[dim_x]--;
      if (dest[dim_y] != 1 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_y]--;
      }
      if (dest[dim_x] != 1 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_x]--;
      }
      break;
    case '8':
    case 'k':
    case KEY_UP:
      tmp[dim_y]--;
      if (dest[dim_y] != 1 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_y]--;
      }
      break;
    case '9':
    case 'u':
    case KEY_PPAGE:
      tmp[dim_y]--;
      tmp[dim_x]++;
      if (dest[dim_y] != 1 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_y]--;
      }
      if (dest[dim_x] != DUNGEON_X - 2 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_x]++;
      }
      break;
    case '6':
    case 'l':
    case KEY_RIGHT:
      tmp[dim_x]++;
      if (dest[dim_x] != DUNGEON_X - 2 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_x]++;
      }
      break;
    case '3':
    case 'n':
    case KEY_NPAGE:
      tmp[dim_y]++;
      tmp[dim_x]++;
      if (dest[dim_y] != DUNGEON_Y - 2 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_y]++;
      }
      if (dest[dim_x] != DUNGEON_X - 2 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_x]++;
      }
      break;
    case '2':
    case 'j':
    case KEY_DOWN:
      tmp[dim_y]++;
      if (dest[dim_y] != DUNGEON_Y - 2 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_y]++;
      }
      break;
    case '1':
    case 'b':
    case KEY_END:
      tmp[dim_y]++;
      tmp[dim_x]--;
      if (dest[dim_y] != DUNGEON_Y - 2 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_y]++;
      }
      if (dest[dim_x] != 1 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_x]--;
      }
      break;
    case '4':
    case 'h':
    case KEY_LEFT:
      tmp[dim_x]--;
      if (dest[dim_x] != 1 &&
          can_see(d, d->PC->position, tmp, 1, 0)) {
        dest[dim_x]--;
      }
      break;
    }
  } while ((c == 't' && (!charpair(dest) || charpair(dest) == d->PC)) ||
           (c != 't' && c != 27 /* ESC */));

  if (c == 27 /* ESC */) {
    io_display(d);
    return 1;
  }
   
  mvprintw(3, 10, " %-44s ", "MONSTER DESCRIPTION (Press any key to resume)"); 
  mvprintw(4 , 10, " %-44s ", DIVIDER_44);
  attron(COLOR_PAIR(charpair(dest)->get_color()));
  mvprintw(5, 10, " %-44s ", charpair(dest)->name);
  attroff(COLOR_PAIR(charpair(dest)->get_color()));
  mvprintw(6, 10, " %-44s ", " ");
  npc *monster = (npc*) charpair(dest);
  std::vector<std::string> mons_desc = split(monster->description, 45);
  for (i = 0; i < mons_desc.size(); i++){
    mvprintw(i + 7, 10, " %-44s ", mons_desc[i].c_str());
  } mvprintw(i + 7, 10, " %-44s ", DIVIDER_44);
  refresh();
  getch();

  io_display(d);

  return 0;  
}

static uint32_t io_inspect_eq(dungeon_t *d)
{
  uint32_t i, key;
  char s[61], t[61];

  for (i = 0; i < num_eq_slots; i++) {
    sprintf(s, "[%s]", eq_slot_name[i]);
    io_object_to_string(d->PC->eq[i], t, 61, d);
    mvprintw(i + 5, 10, " %c %-9s) %-45s ", 'a' + i, s, t);
  }
  mvprintw(17, 10, " %-58s ", "");
  mvprintw(18, 10, " %-58s ", "Inspect which item (ESC to cancel, '/' for inventory)?");
  refresh();

  while (1) {
    if ((key = getch()) == 27 /* ESC */) {
      io_display(d);
      return 1;
    }

    if (key == '/') {
      io_display(d);
      io_inspect_in(d);
      return 1;
    }

    if (key < 'a' || key > 'l') {
      if (isprint(key)) {
        snprintf(s, 61, "Invalid input: '%c'.  Enter a-l or ESC to cancel.",
                 key);
        mvprintw(18, 10, " %-58s ", s);
        mvprintw(19, 10, " %-58s ", DIVIDER);
      } else {
        mvprintw(18, 10, " %-58s ",
                 "Invalid input.  Enter a-l or ESC to cancel.");
        mvprintw(19, 10, " %-58s ", DIVIDER);
      }
      refresh();
      continue;
    }

    if (!d->PC->eq[key - 'a']) {
      mvprintw(18, 10, " %-58s ", "Empty equipment slot.  Try again.");
      mvprintw(19, 10, " %-58s ", DIVIDER);
      continue;
    }

    io_display(d);
    io_display_obj_info(d->PC->eq[key - 'a']);
    io_display(d);
    return 1;
  }

  return 1;
}

uint32_t io_expunge_in(dungeon_t *d)
{
  uint32_t i, key;
  char s[61];
  mvprintw(2, 10, " %-58s ", " ");
  mvprintw(3, 10, " %-58s ", "REMOVE AN ITEM                            (ESC to cancel)");
  mvprintw(4, 10, " %-58s ", DIVIDER);
  for (i = 0; i < MAX_INVENTORY; i++) {
    /* We'll write 12 lines, 10 of inventory, 1 blank, and 1 prompt. *
     * We'll limit width to 60 characters, so very long object names *
     * will be truncated.  In an 80x24 terminal, this gives offsets  *
     * at 10 x and 6 y to start printing things.                     */
      if (d->PC->in[i]) { attron(COLOR_PAIR(d->PC->in[i]->get_color())); }
      mvprintw(i + 5, 10, " %c) %-55s ", '0' + i,
               d->PC->in[i] ? d->PC->in[i]->get_name() : "");
      if (d->PC->in[i]) { attroff(COLOR_PAIR(d->PC->in[i]->get_color())); }
     mvprintw(i + 5, 10, " %c)", '0' + i);
  }
  mvprintw(15, 10, " %-58s ", DIVIDER);
  refresh();

  while (1) {
    if ((key = getch()) == 27 /* ESC */) {
      io_display(d);
      return 1;
    }

    if (key < '0' || key > '9') {
      if (isprint(key)) {
        snprintf(s, 61, "Invalid input: '%c'.  Enter 0-9 or ESC to cancel.",
                 key);
        mvprintw(15, 10, " %-58s ", DIVIDER);
        mvprintw(16, 10, " %-58s ", s);
        mvprintw(17, 10, " %-58s ", DIVIDER);
      } else {
        mvprintw(15, 10, " %-58s ", DIVIDER);
        mvprintw(16, 10, " %-58s ",
                 "Invalid input.  Enter 0-9 or ESC to cancel.");
        mvprintw(17, 10, " %-58s ", DIVIDER);
      }
      refresh();
      continue;
    }

    if (!d->PC->in[key - '0']) {
      mvprintw(15, 10, " %-58s ", DIVIDER);
      mvprintw(16, 10, " %-58s ", "Empty inventory slot.  Try again.");
      mvprintw(17, 10, " %-58s ", DIVIDER); 
      continue;
    }

    if (!d->PC->destroy_in(key - '0')) {
      io_display(d);

      return 1;
    }

    snprintf(s, 61, "Can't destroy %s.  Try again.",
             d->PC->in[key - '0']->get_name());
    mvprintw(15, 10, " %-58s ", s); 
    mvprintw(16, 10, " %-58s ", DIVIDER);   
    refresh();
  }

  return 1;
}

/* 
   Just for safekeeping

   mvprintw(0, 0, "%s", "_______    Arrow keys (<- & ->) to navigate, 'p' to enter, ESC to exit   _______");
   mvprintw(1, 0, "%s", "|-----|    _____             _____            _____             _____    |-----|");
   mvprintw(2, 0, "%s", "|     |    |---|    _____    |---|    ____    |---|    _____    |---|    |     |");
   mvprintw(3, 0, "%s", "|     |____|   |____|---|____|   |____|--|____|   |____|---|____|   |____|     |");
   mvprintw(4, 0, "%s", "|______________________________________________________________________________|");
   mvprintw(5, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
   mvprintw(6, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");                                   
   mvprintw(7, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
   mvprintw(8, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
   mvprintw(9, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(10, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(11, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(12, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(13, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(14, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(15, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(16, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(17, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");  
  mvprintw(18, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(19, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(20, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(21, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(22, 0, "%s", "                                                                                ");
  mvprintw(23, 0, "%s", "                                                                                ");

_  _ ____ ____ _  _ ____ ___ 
|\/| |__| |__/ |_/  |___  |  
|  | |  | |  \ | \_ |___  |  

___  ___  ___  ______ _   __ _____ _____ 
|  \/  | / _ \ | ___ \ | / /|  ___|_   _|
| .  . |/ /_\ \| |_/ / |/ / | |__   | |  
| |\/| ||  _  ||    /|    \ |  __|  | |  
| |  | || | | || |\ \| |\  \| |___  | |  
\_|  |_/\_| |_/\_| \_\_| \_/\____/  \_/  

*/

static void io_print_blank_market()
{  
   mvprintw(0, 0, "%s", "_______         Arrow keys to navigate, 'p' to enter, ESC to exit        _______");
   mvprintw(1, 0, "%s", "|-----|    _____             _____            _____             _____    |-----|");
   mvprintw(2, 0, "%s", "|     |    |---|    _____    |---|    ____    |---|    _____    |---|    |     |");
   mvprintw(3, 0, "%s", "|     |____|   |____|---|____|   |____|--|____|   |____|---|____|   |____|     |");
   mvprintw(4, 0, "%s", "|______________________________________________________________________________|");
   mvprintw(5, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
   mvprintw(6, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");                                   
   mvprintw(7, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
   mvprintw(8, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
   mvprintw(9, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(10, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(11, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(12, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(13, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(14, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(15, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(16, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(17, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");  
  mvprintw(18, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(19, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(20, 0, "%s", "|____|______|____|______|____|______|____|______|____|______|____|______|____|_|");
  mvprintw(21, 0, "%s", "|______|____|______|____|______|____|______|____|______|____|______|____|______|");
  mvprintw(22, 0, "%s", "                                                                                ");
  mvprintw(23, 0, "%s", "                                                                                ");
  mvprintw(4, 18, " %s ", "___  ___  ___  ______ _   __ _____ _____ ");
  mvprintw(5, 18, " %s ", "|  \\/  | / _ \\ | ___ \\ | / /|  ___|_   _|");
  mvprintw(6, 18, " %s ", "| .  . |/ /_\\ \\| |_/ / |/ / | |__   | |  ");
  mvprintw(7, 18, " %s ", "| |\\/| ||  _  ||    /|    \\ |  __|  | |  ");
  mvprintw(8, 18, " %s ", "| |  | || | | || |\\ \\| |\\  \\| |___  | |  ");
  mvprintw(9, 18, " %s ", "\\_|  |_/\\_| |_/\\_| \\_\\_| \\_/\\____/  \\_/ ");
 mvprintw(10, 18, " %s ", "_________________________________________");
}

static void io_print_pc_stats(dungeon *d)
{
  int i, damage; 
  int num_eq_slots = 12;

  mvprintw(11, 44, " %-18s ", " ");
  attron(COLOR_PAIR(COLOR_CYAN));
  mvprintw(12, 44, " %-18s ", "    YOUR STATS    ");
  attroff(COLOR_PAIR(COLOR_CYAN));
  mvprintw(13, 44, " %-18s ", "~~~~~~~~~~~~~~~~~~");
  mvprintw(14, 44, " %-18s ", " ");
  mvprintw(15, 44, " %-18s ", " ");
  mvprintw(16, 44, " %-18s ", " ");
  mvprintw(17, 44, " %-18s ", " ");
  mvprintw(18, 44, " %-18s ", " ");
  mvprintw(19, 44, " %-18s ", " ");
  mvprintw(20, 44, "%s" , "___ ______ ____ __");

  mvprintw(14, 44, " %-7s %2d %s", "Wallet:", d->PC->get_gold_count(), "ingots");
  mvprintw(15, 44, " %-10s %d", "HP:", d->PC->hp);
  for (i = damage = 0; i < num_eq_slots; i++) {
    if (i == eq_slot_weapon && !d->PC->eq[i]) {
      damage += d->PC->damage->roll();
    } else if (d->PC->eq[i]) {
      damage += d->PC->eq[i]->roll_dice();
    }
  }
  mvprintw(16, 44, " %-10s %d", "Damage:", damage);
  mvprintw(17, 44, " %-10s %d", "Speed:", character_get_speed(d->PC));
  mvprintw(19, 44, " %-18s ", "~~~~~~~~~~~~~~~~~~");
  for (i = 0; i < 10; i++){
    mvprintw(11 + i, 43, "%s", "|");
  }

  return;
}

static void io_cant_buy(object *o)
{
  int i;
  for (i = 11; i < 20; i++){mvprintw(i, 11, " %-30s ", " ");}
  attron(COLOR_PAIR(o->get_color()));
  mvprintw(12, 11, " %-30s ", o->get_name());
  attroff(COLOR_PAIR(o->get_color()));
  mvprintw(13, 11, " %-30s ", "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
  mvprintw(14, 11, " You cannot buy this item."); 
  mvprintw(15, 11, " Press any key to continue."); 
  mvprintw(16, 11, " %-30s ", "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");

  return;
}

static void io_cant_sell(object *o)
{
  int i;
  for (i = 11; i < 20; i++){mvprintw(i, 11, " %-30s ", " ");}
  attron(COLOR_PAIR(o->get_color()));
  mvprintw(12, 11, " %-30s ", o->get_name());
  attroff(COLOR_PAIR(o->get_color()));
  mvprintw(13, 11, " %-30s ", "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
  mvprintw(14, 11, " You cannot sell this item."); 
  mvprintw(15, 11, " Press any key to continue."); 
  mvprintw(16, 11, " %-30s ", "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");

  return;
}

static void io_take_away_gold(dungeon *d, int value)
{
  object *ingot;
  int i;
  for (i = value; i > 0; i--){
    ingot = d->PC->in[d->PC->get_gold_slot()];
    while (ingot->get_next()->get_next() != NULL){
      ingot = ingot->get_next();
    }
    ingot->set_next(NULL);
  }
  return;
}

static void io_buy_item(object *o, dungeon *d)
{
  if ((uint32_t)d->PC->get_gold_count() < (uint32_t)(o->get_value() / 100)){ io_cant_buy(o); return;}
  if (!(d->PC->has_open_inventory_slot())){ io_cant_buy(o); return;}
  int value, i;
  value = o->get_value() / 100; 
  if (value == 0){ io_cant_buy(o); return; }
  io_take_away_gold(d, value);
  d->PC->in[d->PC->get_first_open_inventory_slot()] = o;

  for (i = 11; i < 20; i++){mvprintw(i, 11, " %-30s ", " ");}
  attron(COLOR_PAIR(o->get_color()));
  mvprintw(12, 11, " %-30s ", o->get_name());
  attroff(COLOR_PAIR(o->get_color()));
  mvprintw(13, 11, " %-30s ", "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
  mvprintw(14, 11, " You bought %d for %2d ingots.", 1, o->get_value() / 100);  
  mvprintw(15, 11, " Press any key to continue.");
  mvprintw(16, 11, " %-30s ", "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
  return;
}

static void io_print_market_items(dungeon *d)
{
  std::vector<object*> market_items;
  uint32_t i;
  object *o;
  for (i = 0; i < 2; i++){
    o = gen_random_potion(d, market_items);
    market_items.push_back(o);
  }
  for (i = 0; i < 2; i++){
    o = gen_random_weapon(d, market_items);
    market_items.push_back(o);
  }
  for (i = 0; i < 2; i++){
    o = gen_random_acces(d, market_items);
    market_items.push_back(o);
  } 
  mvprintw(10, 8, " %s ", "___|____|_________________________");
  mvprintw(11, 11, " %s ", "~~~~~~~~~~~ POTIONS ~~~~~~~~~~"); 
  mvprintw(14, 11, " %s ", "~~~~~~~~~~~ WEAPONS ~~~~~~~~~~"); 
  mvprintw(17, 11, " %s ", "~~~~~~~~~~~ ACCESS. ~~~~~~~~~~");
  attron(COLOR_PAIR(COLOR_CYAN));
  mvprintw(11, 23, " %s ", "POTIONS");
  mvprintw(14, 23, " %s ", "WEAPONS");
  mvprintw(17, 23, " %s ", "ACCESS.");
  attroff(COLOR_PAIR(COLOR_CYAN));
  for (i = 0; i < 2; i++){
    std::string name(market_items[i]->get_name());
    mvprintw(12 + i, 11, "                                 ");
    attron(COLOR_PAIR(market_items[i]->get_color()));
    mvprintw(12 + i, 11, " %-20s ", name.c_str());
    attroff(COLOR_PAIR(market_items[i]->get_color()));
  }
  for (i = 0; i < 2; i++){
    std::string name(market_items[i + 2]->get_name());
    mvprintw(15 + i, 11, "                                 ");
    attron(COLOR_PAIR(market_items[i + 2]->get_color()));
    mvprintw(15 + i, 11, " %-20s ", name.c_str());
    attroff(COLOR_PAIR(market_items[i + 2]->get_color()));
  }
  for (i = 0; i < 2; i++){
    std::string name(market_items[i + 4]->get_name());
    mvprintw(18 + i, 11, "                                 ");
    attron(COLOR_PAIR(market_items[i + 4]->get_color()));
    mvprintw(18 + i, 11, " %-20s ", name.c_str());
    attroff(COLOR_PAIR(market_items[i + 4]->get_color()));
  } mvprintw (18 + i, 11, " ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ");
  for (i = 0; i < 10; i++){
    mvprintw(11 + i, 43, "%s", "|");
  }
  int c; i = 12;
  while (c != 27 && c != 'p'){
    if (c == KEY_UP && i > 12){ i--; }
    if (c == KEY_DOWN && i < 19){ i++; }

    mvprintw(i, 42, "%c", '<'); 
    refresh(); c = getch();
    mvprintw(i, 42, "%c", ' ');
  }
  if (c == 'p' && (i != 2 || i != 4 || i != 6)){ io_buy_item(market_items[i - 12], d); }

  return;
}

static void io_print_item_to_sell(object *o)
{
  uint32_t i;
  for (i = 13; i < 20; i++){mvprintw(i, 11, " %-30s", "                               ");}
  attron(COLOR_PAIR(o->get_color()));
  mvprintw(13, 11, " %-30s ", o->get_name());
  attroff(COLOR_PAIR(o->get_color()));
  mvprintw(14, 11, " %-30s",  "- - - - - - - - - - - - - - - -");
  std::vector<std::string> obj_desc = split(o->get_description(), 30);
  for (i = 0; i < obj_desc.size(); i++){
    mvprintw(15 + i, 11, " %-30s ", obj_desc[i].c_str());
    if (i == 3){mvprintw(15 + i, 11, " %-30s", "..."); break;}
  } 
  if (i == obj_desc.size() - 1){
    mvprintw(15 + i, 11, " %-30s", "- - - - - - - - - - - - - - - -");
  } else { mvprintw(15 + i + 1, 11, " %-30s", "- - - - - - - - - - - - - - - -"); }
  return;
}

static void io_add_gold(dungeon *d)
{ 
  pair_t p;
  std::vector<object_description> &v = d->object_descriptions;
  object *ingot = new object(v[1], p, NULL);
  object *o;
  if (d->PC->has_gold_in_inv()){
    o = d->PC->in[d->PC->get_gold_slot()];
    while (o->get_next() != NULL){ o = o->get_next(); }
    o->set_next(ingot);
  } else {
    d->PC->in[d->PC->get_first_open_inventory_slot()] = ingot;
  }

  return;
}

static void io_sell_item(uint32_t count, object *o, dungeon *d)
{
  int i;
  int counter;
  for (i = 0; i < MAX_INVENTORY; i++){
    if (d->PC->in[i]){
      if (d->PC->in[i]->get_name() == o->get_name() && count != 0){
        d->PC->destroy_in(i); count--;
        counter = o->get_value() / 100;
        counter > 99 ? counter = 99 : counter = counter;
        while (counter > 0){io_add_gold(d); counter--;}
      }
    }
  }

  return;
}

static void io_sell_item_prompt(object *o, dungeon *d)
{
  int i, c;
  uint32_t count, value;
  i = count = c = 0;
  for (i = 11; i < 20; i++){mvprintw(i, 11, " %-30s", "                               ");}
  attron(COLOR_PAIR(o->get_color()));
  mvprintw(12, 11, " %-30s ", o->get_name());
  attroff(COLOR_PAIR(o->get_color()));
  mvprintw(13, 11, " %-30s", "- - - - - - - - - - - - - - - -");
  mvprintw(14, 11, " You have: %2d of these ", d->PC->get_count_of(o)); 
  mvprintw(15, 11, " %-30s", "- - - - - - - - - - - - - - - -");
  while(c != 27 && c!= 'p'){
    if (c == KEY_UP && count < 99){ count++; }
    if (c == KEY_DOWN && count > 0){ count--; }
    mvprintw(16, 11, " %-4s: %2d ", "Sell", count);
    value = (count * o->get_value()) / 100;
    mvprintw(17, 11, " %-12s %4d %-7s", "For a profit of:",(value > 99 ? 99 : value), "ingots.");
    refresh(); c = getch();
  }  
  if (value == 0){ io_cant_sell(o); return; }
  if (c == 'p' && count <= d->PC->get_count_of(o)){
    for (i = 11; i < 20; i++){mvprintw(i, 11, " %-30s ", " ");}
    attron(COLOR_PAIR(o->get_color()));
    mvprintw(12, 11, " %-30s ", o->get_name());
    attroff(COLOR_PAIR(o->get_color()));
    mvprintw(13, 11, " %-30s ", "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
    mvprintw(14, 11, " You sold %d for %2d ingots.", count, 
                   (((o->get_value() / 100) > 99) ? 99 : o->get_value() / 100));
    mvprintw(15, 11, " Press any key to continue.");
    mvprintw(16, 11, " %-30s ", "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
    io_sell_item(count, o, d);
  }
  return;
}

static void io_sell_prompt(dungeon *d)
{
  int i, c;
  i = c = 0;
  mvprintw(11, 11, " %-30s ", "~~~ Choose an item to sell ~~~");  
  while (c != 27 && c!= 'p'){
    if (c == KEY_LEFT && i > 0) {i--;}
    if (c == KEY_RIGHT && i < MAX_INVENTORY - 1) {i++;}
    mvprintw(12, 11, " [%1d] %-26s ", i, " ");
    if (!(d->PC->in[i])){
      mvprintw(13, 11, " %-30s ", "~~ Empty slot ~~");
      mvprintw(14, 11, " %-30s",  "- - - - - - - - - - - - - - - -");
      mvprintw(15, 11, " %-30s",  "                               ");
      mvprintw(16, 11, " %-30s",  "                               ");
      mvprintw(17, 11, " %-30s",  "                               ");
      mvprintw(18, 11, " %-30s",  "                               ");
      mvprintw(19, 11, " %-30s",  "- - - - - - - - - - - - - - - -");
    } else if (d->PC->in[i]->get_type() == objtype_GOLD){
      mvprintw(13, 11, " %-30s ", "~~ Empty slot ~~");
      mvprintw(14, 11, " %-30s",  "- - - - - - - - - - - - - - - -");
      mvprintw(15, 11, " %-30s",  "                               ");
      mvprintw(16, 11, " %-30s",  "                               ");
      mvprintw(17, 11, " %-30s",  "                               ");
      mvprintw(18, 11, " %-30s",  "                               ");
      mvprintw(19, 11, " %-30s",  "- - - - - - - - - - - - - - - -");
    } else {
      io_print_item_to_sell(d->PC->in[i]);
    }
    refresh(); c = getch();
  }
  if (c == 'p' && d->PC->in[i]){ io_sell_item_prompt(d->PC->in[i], d); }
  return;
}

static void io_market_buy(dungeon *d)
{
  io_print_blank_market();
  io_print_pc_stats(d);
  io_print_market_items(d);
  refresh(); getch();
  return;
}

static void io_market_sell(dungeon *d)
{
  io_print_blank_market();
  io_print_pc_stats(d);
  io_sell_prompt(d);
  refresh(); getch();
  return;
}

static void io_display_marketplace(dungeon *d)
{
  io_print_blank_market();
  mvprintw(11, 18, "   %-12s", " _______ ");
  mvprintw(12, 18, "   %-12s", "|       |");
  mvprintw(13, 18, "   %-12s", "|  BUY  |");
  mvprintw(14, 18, "   %-12s", "|_______|");

  mvprintw(15, 18, "   %-12s", "         ");

  mvprintw(16, 18, "   %-12s", " _______ ");
  mvprintw(17, 18, "   %-12s", "|       |");
  mvprintw(18, 18, "   %-12s", "| SELL  |");
  mvprintw(19, 18, "   %-12s", "|_______|");
  mvprintw(20, 18, "%-15s", "_ ____ ______ ____");
  io_print_pc_stats(d);
  int c = KEY_UP;
  bool b = false;
  while (c != 27 && c != 'p'){
    if (c == KEY_UP){
      attron(COLOR_PAIR(COLOR_CYAN));
      mvprintw(11, 18, "   %-12s", " _______ ");
      mvprintw(12, 18, "   %-12s", "|       |");
      mvprintw(13, 18, "   %-12s", "|  BUY  |");
      mvprintw(14, 18, "   %-12s", "|_______|");
      attroff(COLOR_PAIR(COLOR_CYAN));
      mvprintw(16, 18, "   %-12s", " _______ ");
      mvprintw(17, 18, "   %-12s", "|       |");
      mvprintw(18, 18, "   %-12s", "| SELL  |");
      mvprintw(19, 18, "   %-12s", "|_______|");
      b = true;
    } else if (c == KEY_DOWN){
      attron(COLOR_PAIR(COLOR_CYAN));
      mvprintw(16, 18, "   %-12s", " _______ ");
      mvprintw(17, 18, "   %-12s", "|       |");
      mvprintw(18, 18, "   %-12s", "| SELL  |");
      mvprintw(19, 18, "   %-12s", "|_______|");
      attroff(COLOR_PAIR(COLOR_CYAN));
      mvprintw(11, 18, "   %-12s", " _______ ");
      mvprintw(12, 18, "   %-12s", "|       |");
      mvprintw(13, 18, "   %-12s", "|  BUY  |");
      mvprintw(14, 18, "   %-12s", "|_______|");
      b = false;
    }
    refresh();
    c = getch();
  }
  if (c != 27){
    if (b){ io_market_buy(d); } else { io_market_sell(d); }
  }
  return; 
}

static void io_use_potion(dungeon *d)
{
  uint32_t i;
  char s[61];
  int c;

  mvprintw(2, 10, " %-58s ", " ");
  mvprintw(3, 10, " %-58s ", "USE A POTION [0..9]                (Press ESC to resume)");
  mvprintw(4, 10, " %-58s ", DIVIDER);
  for (i = 0; i < MAX_INVENTORY; i++) {
    io_object_to_string(d->PC->in[i], s, 61, d);
    if (d->PC->in[i]){ attron(COLOR_PAIR(d->PC->in[i]->get_color())); }
    mvprintw(i + 5, 10, " %c) %-55s ", '0' + i, s);
    if (d->PC->in[i]){ attroff(COLOR_PAIR(d->PC->in[i]->get_color())); }
    mvprintw(i + 5, 10, " %c)", '0' + i);
  }
  mvprintw(15, 10, " %-58s ", DIVIDER);

  refresh();  c = getch();
  while (c != 27){
    if (c < '0' || c > '9'){
      mvprintw(16, 10, " Invalid input: '%c'.  Enter 0-9 or ESC to cancel.", c);
      mvprintw(17, 10, " %-58s ", DIVIDER);
    } else {
      mvprintw(16, 10, "                                                 ");
      mvprintw(17, 10, "                                                 ");
    }
    refresh();
    c = getch();
  }


}

void io_handle_input(dungeon *d)
{
  uint32_t fail_code;
  int key;
  fd_set readfs;
  struct timeval tv;
  uint32_t fog_off = 0;
  pair_t tmp = { DUNGEON_X, DUNGEON_Y };

  do {
    do{
      FD_ZERO(&readfs);
      FD_SET(STDIN_FILENO, &readfs);

      tv.tv_sec = 0;
      tv.tv_usec = 125000; /* An eigth of a second */

      if (fog_off) {
        /* Out-of-bounds cursor will not be rendered. */
        io_redisplay_non_terrain(d, tmp);
      } else {
        io_redisplay_visible_monsters(d, tmp);
      }
    } while (!select(STDIN_FILENO + 1, &readfs, NULL, NULL, &tv));
    fog_off = 0;
    switch (key = getch()) {
    case '7':
    case 'y':
    case KEY_HOME:
      fail_code = move_pc(d, 7);
      break;
    case '8':
    case 'k':
    case KEY_UP:
      fail_code = move_pc(d, 8);
      break;
    case '9':
    case 'u':
    case KEY_PPAGE:
      fail_code = move_pc(d, 9);
      break;
    case '6':
    case 'l':
    case KEY_RIGHT:
      fail_code = move_pc(d, 6);
      break;
    case '3':
    case 'n':
    case KEY_NPAGE:
      fail_code = move_pc(d, 3);
      break;
    case '2':
    case 'j':
    case KEY_DOWN:
      fail_code = move_pc(d, 2);
      break;
    case '1':
    case 'b':
    case KEY_END:
      fail_code = move_pc(d, 1);
      break;
    case '4':
    case 'h':
    case KEY_LEFT:
      fail_code = move_pc(d, 4);
      break;
    case '5':
    case ' ':
    case '.':
    case KEY_B2:
      fail_code = 0;
      break;
    case '>':
      fail_code = move_pc(d, '>');
      break;
    case '<':
      fail_code = move_pc(d, '<');
      break;
    case 'Q':
      d->quit = 1;
      fail_code = 0;
      break;
    case 'U':
      io_use_potion(d);
      fail_code = 0;
      break;
    case '+':
      io_display_marketplace(d);
      fail_code = 0;
      break;
    case 'T':
      /* New command.  Display the distances for tunnelers.             */
      io_display_tunnel(d);
      fail_code = 1;
      break;
    case 'D':
      /* New command.  Display the distances for non-tunnelers.         */
      io_display_distance(d);
      fail_code = 1;
      break;
    case 'H':
      /* New command.  Display the hardnesses.                          */
      io_display_hardness(d);
      fail_code = 1;
      break;
    case 's':
      /* New command.  Return to normal display after displaying some   *
       * special screen.                                                */
      io_display(d);
      fail_code = 1;
      break;
    case 'g':
      /* Teleport the PC to a random place in the dungeon.              */
      io_teleport_pc(d);
      fail_code = 1;
      break;
    case 'm':
      io_list_monsters(d);
      fail_code = 1;
      break;
    case 'f':
      io_display_no_fog(d);
      fog_off = 1;
      fail_code = 1;
      break;
    case 'w':
      fail_code = io_wear_eq(d);
      break;
    case 't':
      fail_code = io_remove_eq(d);
      break;
    case 'd':
      fail_code = io_drop_in(d);
      break;
    case 'x':
      fail_code = io_expunge_in(d);
      break;
    case 'i':
      io_display_in(d);
      fail_code = 1;
      break;
    case 'e':
      io_display_eq(d);
      fail_code = 1;
      break;
    case 'c':
      io_display_ch(d);
      fail_code = 1;
      break;
    case 'I':
      io_inspect_in(d);
      fail_code = 1;
      break;
    case 'L':
      io_inspect_monster(d);
      fail_code = 1;
      break;
    case 'q':
      /* Demonstrate use of the message queue.  You can use this for *
       * printf()-style debugging (though gdb is probably a better   *
       * option.  Not that it matterrs, but using this command will  *
       * waste a turn.  Set fail_code to 1 and you should be able to *
       * figure out why I did it that way.                           */
      io_queue_message("This is the first message.");
      io_queue_message("Since there are multiple messages, "
                       "you will see \"more\" prompts.");
      io_queue_message("You can use any key to advance through messages.");
      io_queue_message("Normal gameplay will not resume until the queue "
                       "is empty.");
      io_queue_message("Long lines will be truncated, not wrapped.");
      io_queue_message("io_queue_message() is variadic and handles "
                       "all printf() conversion specifiers.");
      io_queue_message("Did you see %s?", "what I did there");
      io_queue_message("When the last message is displayed, there will "
                       "be no \"more\" prompt.");
      io_queue_message("Have fun!  And happy printing!");
      fail_code = 0;
      break;
    default:
      /* Also not in the spec.  It's not always easy to figure out what *
       * key code corresponds with a given keystroke.  Print out any    *
       * unhandled key here.  Not only does it give a visual error      *
       * indicator, but it also gives an integer value that can be used *
       * for that key in this (or other) switch statements.  Printed in *
       * octal, with the leading zero, because ncurses.h lists codes in *
       * octal, thus allowing us to do reverse lookups.  If a key has a *
       * name defined in the header, you can use the name here, else    *
       * you can directly use the octal value.                          */
      mvprintw(0, 0, "Unbound key: %#o ", key);
      fail_code = 1;
    }
  } while (fail_code);
}
