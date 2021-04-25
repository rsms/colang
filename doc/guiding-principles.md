# Philosophy and guiding principles

- It's a tool for getting the job done; a means to an end
- Explicit is better than implicit
- Simple is better than complex
- Simple is better than easy
- Simplicity does not mean easy, but it may mean straight-forward or uncomplicated
- Just because something may be simple, don’t mistake it for crude
- Simplicity is a goal, not a by-product
- Choose simplicity over completeness. There is an exponential cost in completeness.
- Complex is better than complicated
- Flat is better than nested
- Special cases aren't special enough to break the rules, although practicality beats purity
- Errors should never pass silently, unless explicitly silenced
- Mutable state is hard
- Immutable data can be safely shared
- Isolated data is safe


## Sources of inspiration

### Simplicity and collaboration

every language starts out with simplicity as a goal, yet many of them fail to achieve this goal. Eventually falling back on notions of expressiveness or power of the language as justification for a failure to remain simple.

Any why is this ? Why do so many language, launched with sincere, idealistic goals, fall afoul of their own self inflicted complexity ?

One reason, one major reason, I believe, is that to be thought successful, a language should somehow include all the popular features of its predecessors.
If you would listen to language critics, they demand that any new language should push forward the boundaries of language theory.

In reality this appears to be a veiled request that your new language include all the bits they felt were important in their favourite old language, while still holding true to the promise of whatever it was that drew them to investigate your language I the first place.

I believe that this is a fundamentally incorrect view.

Why would a new language be proposed if not to address limitations of its predecessors ?

Why should a new language not aim to represent a refinement of the cornucopia of features presented in existing languages, learning from its predecessors, rather than repeating their folly.

Language design is about trade-offs; you cannot have your cake and eat it too. So I challenge the notion that every mainstream language must be a super-set of those it seeks to replace.

[...]

Go is a language that chooses to be simple, and it does so by choosing to not include many features that other programming languages have accustomed their users to believing are essential.

[...]

A lack of simplicity is, of course complexity.

Complexity is friction, a force which acts against getting things done.

Complexity is debt, it robs you of capital to invest in the future.

[...]

Good programmers write simple programs.

They bring their experience, their knowledge and their failures to new designs, to learn from and avoid mistakes in the future.

[...]

> “Simplicity is the ultimate sophistication” — Leonardo da Vinci

_[Simplicity and collaboration, Dave Cheney](https://dave.cheney.net/2015/03/08/simplicity-and-collaboration)_


### The Zen of Python

Beautiful is better than ugly.<br>
Explicit is better than implicit.<br>
Simple is better than complex.<br>
Complex is better than complicated.<br>
Flat is better than nested.<br>
Sparse is better than dense.<br>
Readability counts.<br>
Special cases aren't special enough to break the rules.<br>
Although practicality beats purity.<br>
Errors should never pass silently.<br>
Unless explicitly silenced.<br>
In the face of ambiguity, refuse the temptation to guess.<br>
There should be one-- and preferably only one --obvious way to do it.<br>
Although that way may not be obvious at first unless you're Dutch.<br>
Now is better than never.<br>
Although never is often better than *right* now.<br>
If the implementation is hard to explain, it's a bad idea.<br>
If the implementation is easy to explain, it may be a good idea.<br>
Namespaces are one honking great idea -- let's do more of those!<br>

_[The Zen of Python, Tim Peters](https://www.python.org/dev/peps/pep-0020/)_


### The Pony Philosophy

Order of importance:
1. Correctness
2. Performance
3. Simplicity
4. Consistency
5. Completeness

#### Correctness
Incorrectness is simply not allowed. It’s pointless to try to get stuff done if you can’t guarantee the result is correct.

#### Performance
Runtime speed is more important than everything except correctness. If performance must be sacrificed for correctness, try to come up with a new way to do things. The faster the program can get stuff done, the better. This is more important than anything except a correct result.

#### Simplicity
Simplicity can be sacrificed for performance. It is more important for the interface to be simple than the implementation. The faster the programmer can get stuff done, the better. It’s ok to make things a bit harder on the programmer to improve performance, but it’s more important to make things easier on the programmer than it is to make things easier on the language/runtime.

#### Consistency
Consistency can be sacrificed for simplicity or performance. Don’t let excessive consistency get in the way of getting stuff done.

#### Completeness
It’s nice to cover as many things as possible, but completeness can be sacrificed for anything else. It’s better to get some stuff done now than wait until everything can get done later.
The “get-stuff-done” approach has the same attitude towards correctness and simplicity as “the-right-thing”, but the same attitude towards consistency and completeness as “worse-is-better”. It also adds performance as a new principle, treating it as the second most important thing (after correctness).

#### Pony Guiding Principles

Throughout the design and development of the language, the following principles should be adhered to.

- Use the get-stuff-done approach.
- Simple grammar. Language must be trivial to parse for both humans and computers.
- No loadable code. Everything is known to the compiler.
- Fully type safe. There is no “trust me, I know what I’m doing” coercion.
- Fully memory safe. There is no “this random number is really a pointer, honest.”
- No crashes. A program that compiles should never crash (although it may hang or do something unintended).
- Sensible error messages. Where possible use simple error messages for specific error cases. It is fine to assume the programmer knows the definitions of words in our lexicon, but avoid compiler or other computer science jargon.
- Inherent build system. No separate applications required to configure or build.
- Aim to reduce common programming bugs through the use of restrictive syntax.
- Provide a single, clean and clear way to do things rather than catering to every programmer’s preferred prejudices.
- Make upgrades clean. Do not try to merge new features with the ones they are replacing, if something is broken remove it and replace it in one go. Where possible provide rewrite utilities to upgrade source between language versions.
- Reasonable build time. Keeping down build time is important, but less important than runtime performance and correctness.
- Allowing the programmer to omit some things from the code (default arguments, type inference, etc) is fine, but fully specifying should always be allowed.
- No ambiguity. The programmer should never have to guess what the compiler will do, or vice-versa.
- Document required complexity. Not all language features have to be trivial to understand, but complex features must have full explanations in the docs to be allowed in the language.
- Language features should be minimally intrusive when not used.
- Fully defined semantics. The semantics of all language features must be available in the standard language docs. It is not acceptable to leave behavior undefined or “implementation dependent”.
- Efficient hardware access must be available, but this does not have to pervade the whole language.
- The standard library should be implemented in Pony.
- Interoperability. Must be interoperable with other languages, but this may require a shim layer if non-primitive types are used.
- Avoid library pain. Use of 3rd party Pony libraries should be as easy as possible, with no surprises. This includes writing and distributing libraries and using multiple versions of a library in a single program.


_[The Pony Philosophy](https://www.ponylang.io/discover/#the-pony-philosophy)_
