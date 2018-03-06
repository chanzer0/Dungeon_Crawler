#Dungeon Crawlers

##Running the game
To run the game, you must have a valid C compiler. Download the files as a .zip or clone the repository to your machine. To compile, open a shell and type 'make' into the command line. This will execute the Makefile and compile all necessary objects and binaries.

##Playing the game
You play as the PC - '@'. Your goal is to kill all the monsters - (0-9, a-f). The different characters represent different monster types. Monsters have a 50% probability of each of the following abilities.
+Intelligence: Intelligent monsters move on the shortest path to the PC, and can remember the last position they saw the PC at.
+Telepathy: Telepathic monsters always know where the PC is and will always move toward you.'
+Tunneling: Tunneling monsters can tunnel through otherwise non-manuverable rock. Tunneling creates a new corridor for any monster or PC to travel through.
+Erraticism: Erratic monsters either make a random move or move as per their other characteristic(s).

'>' and '<' represent staircases - they allow you to flee the current dungeon and enter a completely new one.