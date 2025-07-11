testsource

Testing source for webmud (and probably any other mud clients out there)

Opens up ports 8888 and 8899. Anything that connects there will spit back messages one per second. Assumes you have your local /usr/share/dict/words installed.

Every connection will get exactly the same output in the same order every time. (Seeded random number generation is fun!)

Dogfood for crankshaft: https://github.com/ZedrikCayne/crankshaft

You only need to have development ssl libraries installed, everything else is built-in.

And you will have to build the crankshaft once.

