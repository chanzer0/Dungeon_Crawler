#include <vector>
#include <cstring>
#include "ncurses.h"

#include "object.h"
#include "dungeon.h"
#include "utils.h"

object::object(object_description &o, pair_t p, object *next) :
  name(o.get_name()),
  description(o.get_description()),
  type(o.get_type()),
  color(o.get_color()),
  damage(o.get_damage()),
  hit(o.get_hit().roll()),
  dodge(o.get_dodge().roll()),
  defence(o.get_defence().roll()),
  weight(o.get_weight().roll()),
  speed(o.get_speed().roll()),
  attribute(o.get_attribute().roll()),
  value(o.get_value().roll()),
  seen(false),
  next(next),
  od(o)
{
  position[dim_x] = p[dim_x];
  position[dim_y] = p[dim_y];

  od.generate();
}

object::~object()
{
  od.destroy();
  if (next) {
    delete next;
  }
}

static bool is_new(object_description d, std::vector<object*> obj_list)
{
  for (uint32_t i = 0; i < obj_list.size(); i++){
    if (obj_list[i]->get_name() == d.get_name()){ return false; }
  }

  return true;
}

object* gen_random_weapon(dungeon_t *d, std::vector<object*> obj_list)
{
  object *o;
  pair_t p;
  std::vector<object_description> &v = d->object_descriptions;
  int i;
  do {
    i = rand_range(0, v.size() - 1);
    
  } while (v[i].get_type() != objtype_WEAPON || !(is_new(v[i], obj_list)));
  o = new object(v[i], p, NULL);
  return o;
}

object* gen_random_potion(dungeon_t *d, std::vector<object*> obj_list)
{
  object *o;
  pair_t p;
  std::vector<object_description> &v = d->object_descriptions;
  int i;
  do {
    i = rand_range(0, v.size() - 1);
  } while (v[i].get_type() != objtype_POTION || !(is_new(v[i], obj_list)));
  o = new object(v[i], p, NULL);
  return o;
}

object* gen_random_acces(dungeon_t *d, std::vector<object*> obj_list)
{
  object *o;
  pair_t p;
  std::vector<object_description> &v = d->object_descriptions;
  int i;
  do {
    i = rand_range(0, v.size() - 1);
  } while ((v[i].get_type() != objtype_RING && v[i].get_type() != objtype_ARMOR && v[i].get_type() != objtype_LIGHT) || !(is_new(v[i], obj_list)));
  o = new object(v[i], p, NULL);
  return o;
}

void gen_object(dungeon_t *d)
{
  object *o;
  uint32_t room;
  pair_t p;
  std::vector<object_description> &v = d->object_descriptions;
  int i;

  do {
    i = rand_range(0, v.size() - 1);
  } while (!v[i].can_be_generated() || !v[i].pass_rarity_roll() || v[i].get_type() == objtype_POTION);
  
  room = rand_range(0, d->num_rooms - 1);
  do {
    p[dim_y] = rand_range(d->rooms[room].position[dim_y],
                          (d->rooms[room].position[dim_y] +
                           d->rooms[room].size[dim_y] - 1));
    p[dim_x] = rand_range(d->rooms[room].position[dim_x],
                          (d->rooms[room].position[dim_x] +
                           d->rooms[room].size[dim_x] - 1));
  } while (mappair(p) > ter_stairs);

  o = new object(v[i], p, d->objmap[p[dim_y]][p[dim_x]]);
  o->set_next(NULL);
  d->objmap[p[dim_y]][p[dim_x]] = o;
  
  
}

void gen_objects(dungeon_t *d)
{
  uint32_t i;

  memset(d->objmap, 0, sizeof (d->objmap));

  for (i = 0; i < d->max_objects; i++) {
    gen_object(d);
  }

  d->num_objects = d->max_objects;
}

char object::get_symbol()
{
  return next ? '&' : object_symbol[type];
}

uint32_t object::get_color()
{
  return color;
}

const char *object::get_name()
{
  return name.c_str();
}

int32_t object::get_speed()
{
  return speed;
}

int32_t object::roll_dice()
{
  return damage.roll();
}

void destroy_objects(dungeon_t *d)
{
  uint32_t y, x;

  for (y = 0; y < DUNGEON_Y; y++) {
    for (x = 0; x < DUNGEON_X; x++) {
      if (d->objmap[y][x]) {
        delete d->objmap[y][x];
        d->objmap[y][x] = 0;
      }
    }
  }
}

int32_t object::get_type()
{
  return type;
}

uint32_t object::is_equipable()
{
  return type >= objtype_WEAPON && type <= objtype_RING; 
}

uint32_t object::is_removable()
{
  return 1;
}

uint32_t object::is_dropable()
{
  return 1;
}

uint32_t object::is_destructable()
{
  return 1;
}

int32_t object::get_eq_slot_index()
{
  if (type < objtype_WEAPON ||
      type > objtype_RING) {
    return -1;
  }

  return type - 1;
}

void object::to_pile(dungeon_t *d, pair_t location)
{
  next = (object *) d->objmap[location[dim_y]][location[dim_x]];
  d->objmap[location[dim_y]][location[dim_x]] = this;
}
