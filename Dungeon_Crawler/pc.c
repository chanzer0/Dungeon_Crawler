#include <stdlib.h>
#include <ncurses.h>

#include "string.h"

#include "dungeon.h"
#include "npc.h"
#include "pc.h"
#include "utils.h"
#include "move.h"
#include "path.h"

void pc_delete(pc_t *pc)
{
  if (pc) {
    free(pc);
  }
}

uint32_t pc_is_alive(dungeon_t *d)
{
  return d->pc.alive;
}

void place_pc(dungeon_t *d)
{
  d->pc.position[dim_y] = rand_range(d->rooms->position[dim_y],
                                     (d->rooms->position[dim_y] +
                                      d->rooms->size[dim_y] - 1));
  d->pc.position[dim_x] = rand_range(d->rooms->position[dim_x],
                                     (d->rooms->position[dim_x] +
                                      d->rooms->size[dim_x] - 1));
}

void config_pc(dungeon_t *d)
{
  memset(&d->pc, 0, sizeof (d->pc));
  d->pc.symbol = '@';

  place_pc(d);

  d->pc.speed = PC_SPEED;
  d->pc.alive = 1;
  d->pc.sequence_number = 0;
  d->pc.pc = calloc(1, sizeof (*d->pc.pc));
  d->pc.npc = NULL;
  d->pc.kills[kill_direct] = d->pc.kills[kill_avenged] = 0;

  d->character[d->pc.position[dim_y]][d->pc.position[dim_x]] = &d->pc;

  dijkstra(d);
  dijkstra_tunnel(d);
}

uint32_t pc_next_pos(dungeon_t *d, pair_t dir)
{
  int flag = 0;
  char ch;
  while (flag != 1){
    ch = getch();
    if ((ch == 'y'|| ch == '7') && 
	d->hardness[d->pc.position[dim_y] - 1][d->pc.position[dim_x] - 1] == 0){ // upper left
      dir[dim_y] = -1;
      dir[dim_x] = -1;
      flag = 1;
    } 
    if ((ch == 'k'|| ch == '8') && 
	d->hardness[d->pc.position[dim_y] - 1][d->pc.position[dim_x]] == 0){ // up
      dir[dim_y] = -1;
      dir[dim_x] = 0;
      flag = 1;
    } 
    if ((ch == 'u'|| ch == '9') && 
	d->hardness[d->pc.position[dim_y] - 1][d->pc.position[dim_x] + 1] == 0){ // upper right
      dir[dim_y] = -1;
      dir[dim_x] = 1;
      flag = 1;
    } 
    if ((ch == 'l'|| ch == '6') && 
	d->hardness[d->pc.position[dim_y]][d->pc.position[dim_x] + 1] == 0) { // right
      dir[dim_x] = 1;
      dir[dim_y] = 0;
      flag = 1;
    } 
    if ((ch == 'n'|| ch == '3') && 
	d->hardness[d->pc.position[dim_y] + 1][d->pc.position[dim_x] + 1] == 0){ // lower right
      dir[dim_y] = 1;
      dir[dim_x] = 1;
      flag = 1;
    } 
    if ((ch == 'j'|| ch == '2') && 
	d->hardness[d->pc.position[dim_y] + 1][d->pc.position[dim_x]] == 0){ // down
      dir[dim_y] = 1;
      dir[dim_x] = 0;
      flag = 1;
    } 
    if ((ch == 'b'|| ch == '1') && 
	d->hardness[d->pc.position[dim_y] + 1][d->pc.position[dim_x] - 1] == 0){ // lower left
      dir[dim_y] = 1;
      dir[dim_x] = -1;
      flag = 1;
    } 
    if ((ch == 'h'|| ch == '4') && 
	d->hardness[d->pc.position[dim_y]][d->pc.position[dim_x] - 1] == 0){ // left
      dir[dim_x] = -1;
      dir[dim_y] = 0;
      flag = 1;
    }
    if (ch == ' '|| ch == '5'){ // take a little nappy-poo
      dir[dim_x] = 0;
      dir[dim_y] = 0;
      flag = 1;
    }
    if ((ch == '<') && 
      d->map[d->pc.position[dim_y]][d->pc.position[dim_x]] == ter_staircase_up){ // staircase up
      /*
       * Delete dungeon, init new one, gen dungeon and monsties, config&place pc
       */
      delete_dungeon(d);
      init_dungeon(d);
      gen_dungeon(d);
      gen_monsters(d);
      config_pc(d);
      flag = 1;
    } 
    if ((ch == '>') && 
      d->map[d->pc.position[dim_y]][d->pc.position[dim_x]] == ter_staircase_down){ // staircase down
      /*
       * Delete dungeon, init new one, gen dungeon and monsties, config&place pc
       */
      delete_dungeon(d);
      init_dungeon(d);
      gen_dungeon(d);
      gen_monsters(d);
      config_pc(d);
      flag = 1;
    } 
    if (ch == 'm'){ // display monstie list
      clear();
      refresh();
      int i = 1;
      int j;
      pair_t p;
      while ((ch != (char) 27)){ // while ch != escape
	
	for (p[dim_y] = 0; p[dim_y] < DUNGEON_Y; p[dim_y]++) {
	  for (p[dim_x] = 0; p[dim_x] < DUNGEON_X; p[dim_x]++) {
	    if (charpair(p) != NULL && charpair(p)->symbol != '@'){
	      if (d->pc.position[dim_y] - p[dim_y] >= 0 && i < 22 &&
		  d->pc.position[dim_x] - p[dim_x] >= 0){ // monstie above and to the right of pc
		mvprintw(i, 0, "%c, %d north and %d west", 
			 charpair(p)->symbol, abs(d->pc.position[dim_y] - p[dim_y]), abs(d->pc.position[dim_x] - p[dim_x]));
		i++;
	      }
	      if (d->pc.position[dim_y] - p[dim_y] <= 0 && i < 22 &&
		  d->pc.position[dim_x] - p[dim_x] >= 0){ // monstie below and to the right of pc
		mvprintw(i, 0, "%c, %d south and %d west", 
			 charpair(p)->symbol, abs(d->pc.position[dim_y] - p[dim_y]), abs(d->pc.position[dim_x] - p[dim_x]));
		i++;
	      }
	      if (d->pc.position[dim_y] - p[dim_y] >= 0 && i < 22 &&
		  d->pc.position[dim_x] - p[dim_x] <= 0){ // monstie above and to the left of pc
		mvprintw(i, 0, "%c, %d north and %d east", 
			 charpair(p)->symbol, abs(d->pc.position[dim_y] - p[dim_y]), abs(d->pc.position[dim_x] - p[dim_x]));
		i++;
	      }
	      if (d->pc.position[dim_y] - p[dim_y] < 0 && i < 22 &&
		  d->pc.position[dim_x] - p[dim_x] < 0){ // monstie below and to the left of pc
		mvprintw(i, 0, "%c, %d south and %d east", 
			 charpair(p)->symbol, abs(d->pc.position[dim_y] - p[dim_y]), abs(d->pc.position[dim_x] - p[dim_x]));
		i++;
	      }
	    }
	  }
	}
	i = 1;
	refresh();
	ch = getch();
      }
      /*
       * After escaping, clear screen and re-render dungeon
       */
      clear();
      render_dungeon(d);
      refresh();
    }
    if (ch == 'q'){ // qq - ggnore
      endwin();
      exit(0);
    }
  }
  /*
  static uint32_t have_seen_corner = 0;
  static uint32_t count = 0;

  dir[dim_y] = dir[dim_x] = 0;

  if (in_corner(d, &d->pc)) {
    if (!count) {
      count = 1;
    }
    have_seen_corner = 1;
  }

   First, eat anybody standing next to us.
  if (charxy(d->pc.position[dim_x] - 1, d->pc.position[dim_y] - 1)) {
    dir[dim_y] = -1;
    dir[dim_x] = -1;
  } else if (charxy(d->pc.position[dim_x], d->pc.position[dim_y] - 1)) {
    dir[dim_y] = -1;
  } else if (charxy(d->pc.position[dim_x] + 1, d->pc.position[dim_y] - 1)) {
    dir[dim_y] = -1;
    dir[dim_x] = 1;
  } else if (charxy(d->pc.position[dim_x] - 1, d->pc.position[dim_y])) {
    dir[dim_x] = -1;
  } else if (charxy(d->pc.position[dim_x] + 1, d->pc.position[dim_y])) {
    dir[dim_x] = 1;
  } else if (charxy(d->pc.position[dim_x] - 1, d->pc.position[dim_y] + 1)) {
    dir[dim_y] = 1;
    dir[dim_x] = -1;
  } else if (charxy(d->pc.position[dim_x], d->pc.position[dim_y] + 1)) {
    dir[dim_y] = 1;
  } else if (charxy(d->pc.position[dim_x] + 1, d->pc.position[dim_y] + 1)) {
    dir[dim_y] = 1;
    dir[dim_x] = 1;
  } else if (!have_seen_corner || count < 250) {
    Head to a corner and let most of the NPCs kill each other off 
    if (count) {
      count++;
    }
    if (!against_wall(d, &d->pc) && ((rand() & 0x111) == 0x111)) {
      dir[dim_x] = (rand() % 3) - 1;
      dir[dim_y] = (rand() % 3) - 1;
    } else {
      dir_nearest_wall(d, &d->pc, dir);
    }
  }else {
     And after we've been there, let's head toward the center of the map. 
    if (!against_wall(d, &d->pc) && ((rand() & 0x111) == 0x111)) {
      dir[dim_x] = (rand() % 3) - 1;
      dir[dim_y] = (rand() % 3) - 1;
    } else {
      dir[dim_x] = ((d->pc.position[dim_x] > DUNGEON_X / 2) ? -1 : 1);
      dir[dim_y] = ((d->pc.position[dim_y] > DUNGEON_Y / 2) ? -1 : 1);
    }
  }

   Don't move to an unoccupied location if that places us next to a monster 
  if (!charxy(d->pc.position[dim_x] + dir[dim_x],
              d->pc.position[dim_y] + dir[dim_y]) &&
      ((charxy(d->pc.position[dim_x] + dir[dim_x] - 1,
               d->pc.position[dim_y] + dir[dim_y] - 1) &&
        (charxy(d->pc.position[dim_x] + dir[dim_x] - 1,
                d->pc.position[dim_y] + dir[dim_y] - 1) != &d->pc)) ||
       (charxy(d->pc.position[dim_x] + dir[dim_x] - 1,
               d->pc.position[dim_y] + dir[dim_y]) &&
        (charxy(d->pc.position[dim_x] + dir[dim_x] - 1,
                d->pc.position[dim_y] + dir[dim_y]) != &d->pc)) ||
       (charxy(d->pc.position[dim_x] + dir[dim_x] - 1,
               d->pc.position[dim_y] + dir[dim_y] + 1) &&
        (charxy(d->pc.position[dim_x] + dir[dim_x] - 1,
                d->pc.position[dim_y] + dir[dim_y] + 1) != &d->pc)) ||
       (charxy(d->pc.position[dim_x] + dir[dim_x],
               d->pc.position[dim_y] + dir[dim_y] - 1) &&
        (charxy(d->pc.position[dim_x] + dir[dim_x],
                d->pc.position[dim_y] + dir[dim_y] - 1) != &d->pc)) ||
       (charxy(d->pc.position[dim_x] + dir[dim_x],
               d->pc.position[dim_y] + dir[dim_y] + 1) &&
        (charxy(d->pc.position[dim_x] + dir[dim_x],
                d->pc.position[dim_y] + dir[dim_y] + 1) != &d->pc)) ||
       (charxy(d->pc.position[dim_x] + dir[dim_x] + 1,
               d->pc.position[dim_y] + dir[dim_y] - 1) &&
        (charxy(d->pc.position[dim_x] + dir[dim_x] + 1,
                d->pc.position[dim_y] + dir[dim_y] - 1) != &d->pc)) ||
       (charxy(d->pc.position[dim_x] + dir[dim_x] + 1,
               d->pc.position[dim_y] + dir[dim_y]) &&
        (charxy(d->pc.position[dim_x] + dir[dim_x] + 1,
                d->pc.position[dim_y] + dir[dim_y]) != &d->pc)) ||
       (charxy(d->pc.position[dim_x] + dir[dim_x] + 1,
               d->pc.position[dim_y] + dir[dim_y] + 1) &&
        (charxy(d->pc.position[dim_x] + dir[dim_x] + 1,
                d->pc.position[dim_y] + dir[dim_y] + 1) != &d->pc)))) {
    dir[dim_x] = dir[dim_y] = 0;
  }
  */

  return 0;
}

uint32_t pc_in_room(dungeon_t *d, uint32_t room)
{
  if ((room < d->num_rooms)                                     &&
      (d->pc.position[dim_x] >= d->rooms[room].position[dim_x]) &&
      (d->pc.position[dim_x] < (d->rooms[room].position[dim_x] +
                                d->rooms[room].size[dim_x]))    &&
      (d->pc.position[dim_y] >= d->rooms[room].position[dim_y]) &&
      (d->pc.position[dim_y] < (d->rooms[room].position[dim_y] +
                                d->rooms[room].size[dim_y]))) {
    return 1;
  }

  return 0;
}
