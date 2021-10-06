Thoughts on linking code

What if instead of object files, we were to maintain a graph database of all
assembled code?

Traditionally a C-like compiler will parse, compile and assemble each source file into
an ELF/Mach-O/etc object file and
finally—when all object files required for a program are available—link them all together
by reading & parsing all these object files just to build a new object (exe) file.
Here's an example of a simple program with four source files:

	    main
	  /  |   \
	foo  bar  baz
	  \  /
	  util

main requires foo, bar and baz. foo and bar both require util.
In practice this is not a tree but a list:

- main -> main.o
- foo -> foo.o
- bar -> bar.o
- baz -> baz.o
- util -> util.o

A C-like compiler would link foo, bar, baz, util and main objects everytime any part changes.
Say we only change baz, we take the cost of re-linking the tree of foo, bar & util.

Imagine if these were represented as a tree even as linked objects, not just temporarily
inside the compiler. Then we could link subtrees together:

	main           = [main.o, foo+util+bar.o, baz.o]
	foo+util+bar.o = [foo.o, bar.o, util.o]
	foo+util.o     = [foo.o, util.o]         # Can be skipped; unused
	foo+util.o     = [bar.o, util.o]         # Can be skipped; unused

If `baz` changes, we can reuse the subtree object `foo+util+bar.o`
