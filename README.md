# Thing

Thing is a small programming language that reads like plain English. Its interpreter is [thing.c](thing.c), written in C, in this folder. Programs are text files ending in `.thing`; [username.thing](username.thing) is a tiny one to start with.

Build the interpreter and run a program:

```bash
gcc -Wall -Wextra -o thing thing.c
./thing username.thing
```

## Starting a project

Like `cargo new` in Rust, `thing new` makes a folder with a hello world already in it, and `thing run` runs it:

```bash
./thing new hello
cd hello
../thing run
```

```text
Hello, world
```

`thing new hello` creates `hello/main.thing` containing `say("Hello, world").` and refuses to touch a folder that already exists. `thing run` runs `main.thing` in the current folder, `thing run other.thing` runs a named file, and `thing program.thing` still works as before. `thing build` checks your program for mistakes, then makes a program that runs on its own, named after the folder (`thing build other.thing` names it `other`). It is the interpreter with your program packed onto the end, so it is not faster, but you can copy it anywhere and run it without the source or the `thing` command. This works on Linux. To type just `thing`, put the built `thing` somewhere on your PATH, such as `~/.local/bin`.

Every example below was run, and the output shown is what it printed. The lessons build on each other, so work through them in order.

## Lesson 1: Saying things

`say("text")` prints plain text. Every statement ends with a period.

```thing
say("Hello, world").
```

```text
Hello, world
```

Anything inside quotes is printed exactly as written, commas included, except for the backslash codes below. The period after the closing quote ends the statement and is not printed.

### Quotes, line breaks and tabs inside text

A backslash writes a character that would otherwise be hard to type:

| Write | Gives |
|---|---|
| `\"` | a double quote |
| `\n` | a line break |
| `\t` | a tab |
| `\r` | a carriage return |
| `\\` | a backslash |

```thing
say("She said \"hi\".\nBye.").
say(split("name\tage", "\t")).
say("{{\"name\": \"Ann\"}}").
```

```text
She said "hi".
Bye.
["name", "age"]
{"name": "Ann"}
```

Any other backslash code, such as `\q`, is an error, so a text with a Windows path needs `"C:\\temp"`. Because `\n` is a real line break, `split(text, "\n")` cuts text into lines and `write` of text with `\n` in it writes several lines.

### Notes

A statement that starts with `Now` or `Next` is a note. Thing skips it, up to the period that ends it. Use `Now` for what is happening here (present tense) and `Next` for what you plan to do or want later (future tense), so your reasons stay beside the code:

```thing
Now, saying hello to the world.
say("Hello, world").
Next, a function will say hello to anyone by name.
```

```text
Hello, world
```

A note can hold commas, quotes and braces, can run over several lines, and ends at the first `.` that is followed by a space or the end of the file, so write `3.5` but avoid a full stop in the middle of a note. It can start a statement, or follow the comma that begins a function's body. A note with no period is an error.

`thing notes` lists every `Next` note in `main.thing` (or in the file you name) with its line number, so the plan written beside the code becomes a to-do list:

```bash
./thing notes grade_report.thing
```

```text
grade_report.thing:83: a built-in round and a way to sort a Book by its values would shorten most of this.
```

`Now` notes are not listed, since they only describe what is already there.

## Lesson 2: Names

A name holds a value. `A` makes one (`An` reads better before a vowel, as in `An age`), `holds` puts a value in, and you use the same `holds` to change it later.

```thing
A name holds "Gary".
An age holds 30.
say(name).
say(age).
age holds 31.
say(age).
```

```text
Gary
30
31
```

`say(...)` prints whatever is inside the parentheses, so it works the same for a name, a calculation or a list.

A name can hold four kinds of value, and it can change kind whenever you like:

| Kind | Looks like |
|---|---|
| Number | `5`, `3.14`, `-2` |
| Words | `"hello"` |
| Yes or No | `Yes`, `No` |
| Nothing | `nil` |

A name made with no value starts out as `nil`:

```thing
A later.
say(later).
later holds 5.
say(later).
```

```text
nil
5
```

## Lesson 3: Asking

`ask("question")` prints the question and waits for the user to type a line.

<!-- input: bob -->
```thing
A username ask("Username: "), say(username).
```

```text
Username: bob
```

Notice the comma. A comma means "then, in the same statement", and the period ends the statement. You can also write `A username holds ask("Username: ")`; both mean the same.

An answer is always words, even if the user types digits. Lesson 4 shows how to turn it into a number.

## Lesson 4: Maths

Signs do maths: `+ - * / %`. `%` is the remainder. Multiplying and dividing happen before adding, and parentheses group.

```thing
say(2 + 3 * 4).
say((2 + 3) * 4).
say(7 / 2).
say(7 % 3).
say(-5 + 2).
```

```text
14
20
3.5
1
-3
```

`number(...)` turns words into a number:

<!-- input: 4
5.5 -->
```thing
An a ask("First: "), A b ask("Second: "), say(number(a) + number(b)).
```

```text
First: Second: 9.5
```

Dividing by zero stops the program with an error.

`round(number)` rounds to a whole number, and `round(number, places)` keeps that many decimal places. A half always rounds up, away from zero, so `2.5` becomes `3` and `-2.5` becomes `-3`. A negative number of places rounds to tens, hundreds and so on:

```thing
say(round(85.3333, 1)).
say(round(2.5)).
say(round(2.675, 2)).
say(round(1250, -2)).
```

```text
85.3
3
2.68
1300
```

Rounding works on the digits you would write down, not on how the computer stores them, so `2.675` gives `2.68` rather than `2.67`. Python's `round(2.5)` gives 2 (it rounds ties to the even number), so this differs from Python on purpose.

`fly` and `sink` round in one fixed direction instead of to the nearest. `fly` goes up (towards bigger numbers) and `sink` goes down (towards smaller numbers). A number with nothing to cut off stays where it is, and both take places like `round`:

| | `3.2` | `3.7` | `-3.2` | `-3.7` | `3` |
|---|---|---|---|---|---|
| `fly(x)` | 4 | 4 | -3 | -3 | 3 |
| `sink(x)` | 3 | 3 | -4 | -4 | 3 |
| `round(x)` | 3 | 4 | -3 | -4 | 3 |

```thing
say(fly(3.2)).
say(sink(3.7)).
say(fly(-3.2)).
say(sink(-3.2)).
say(fly(3.14159, 2)).
say(sink(3.14159, 2)).
say(fly(23 / 10)).
```

```text
4
3
-3
-4
3.15
3.14
3
```

`fly(23 / 10)` answers "how many pages of 10 do 23 items need?", and `sink(23 / 10)` answers "how many full pages are there?". If you know `ceiling` and `floor` from another language, `fly` is `ceiling` and `sink` is `floor`, and writing those names gives an error pointing to the right word.

## Lesson 5: Words

Signs are for numbers and words are for words. `plus` joins and `minus` removes the first match.

```thing
A first holds "Ada".
A last holds "Lovelace".
say(first plus " " plus last).
say("hello" minus "l").
say("Total: " plus 2 + 3).
```

```text
Ada Lovelace
helo
Total: 5
```

The last line shows the order of work: signs go first (`2 + 3` is 5) and `plus` joins afterwards. Mixing them up is an error that tells you which one to use, for example `1 plus 2` says to use `+`.

You can also put values straight into text with braces. Anything in `{ }` is worked out and put in place, and `say("...")` prints the result:

```thing
A name holds "Gary".
A n holds 3.
say("Hello {name}, you have {n} items, {n + 1} next, and {{braces}}").
```

```text
Hello Gary, you have 3 items, 4 next, and {braces}
```

Write `{{` and `}}` for a brace itself. Any single value works inside the braces, including positions like `{row:0}`, and a quote inside the braces is written with a backslash, as in `{people[\"name\"]}`.

## Lesson 6: Comparing

Comparing uses words, and the answer is `Yes` or `No`.

| Write | Means |
|---|---|
| `a is b` | the same |
| `a is not b` | different |
| `a is bigger-than b` | larger (numbers), or later in the alphabet (words) |
| `a is smaller-than b` | smaller, or earlier in the alphabet |

```thing
A x holds 139.
A y holds 499.
say(x is y).
say(x is not y).
say(x is smaller-than y).
say("apple" is smaller-than "banana").
```

```text
No
Yes
Yes
Yes
```

`is` asks a question and `holds` puts a value in, so the two never get mixed up. `=`, `<`, `>` and `equals` are not used for comparing or for putting a value in.

## Lesson 7: Deciding

`if` runs its body only when the condition is `Yes`. The body is everything after the comma, up to the period. `if-not` is the other side.

```thing
A x holds 139. A y holds 499.
if x is y, say("Same number"). if-not say("Different").
```

```text
Different
```

For more than two ways, use `instead,` (the word and the comma go together):

```thing
A n holds 5.
if n is bigger-than 10, say("big") instead, n is bigger-than 3, say("medium") instead, n is bigger-than 0, say("small"), if-not say("zero").
```

```text
medium
```

The first condition that is `Yes` wins, and the final `if-not` catches everything else.

## Lesson 8: Joining conditions

Between the `if` and its comma, `and` and `or` join conditions. `and` goes first, and parentheses group.

```thing
An age holds 25.
A member holds Yes.
if age is bigger-than 17 and member is Yes, say("welcome").
if age is smaller-than 10 or age is bigger-than 65, say("discount").
if (age is smaller-than 10 or age is bigger-than 65) and member is Yes, say("member discount").
```

```text
welcome
```

`not` flips a condition, so `not who in skip` is `Yes` when `who` is not in `skip`, and `if not exists("data.csv"), ...` runs when the file is missing. It goes in front of the thing it flips and works before `and` and `or`:

```thing
An age holds 25.
if not age is bigger-than 30 and age is bigger-than 20, say("in your twenties").
```

```text
in your twenties
```

`and`, `or` and `not` mean this only inside a condition. To do two things, put a comma between them (Lesson 9), or start a new statement.

## Lesson 9: Commas and periods

A comma continues a statement and a period ends it. Each part between the commas is a step, and a statement is all the steps up to the period. A period also ends any `if` or loop that the statement started, so the next statement always runs.

```thing
A x holds 1.
if x is 1, say("one"), say("still inside the If").
say("outside, always runs").
```

```text
one
still inside the If
outside, always runs
```

The same rule applies to loops. If you want two separate things to happen, write two statements.

## Lesson 10: Loops

`for every ... in ..., body` repeats the body. It goes through the letters of a word, the items of a list, or the Entries of a Book.

```thing
A word holds "cat".
for every letter in word, say(letter).
```

```text
c
a
t
```

Add up some numbers:

```thing
A total holds 0.
for every n in [1, 2, 3], total holds total + n.
say(total).
```

```text
6
```

`range` makes the list of numbers to loop over, so "do this three times" is a loop like any other. The end is included, which is different from Python:

```thing
for every i in range(3), say("hi {i}").
say(range(2, 5)).
say(range(0, 10, 4)).
say(range(10, 1, -3)).
```

```text
hi 1
hi 2
hi 3
[2, 3, 4, 5]
[0, 4, 8]
[10, 7, 4, 1]
```

`range(end)` counts from 1 to `end`, `range(start, end)` counts up by one, and `range(start, end, step)` counts by the step, which can be negative to count down. They work with whole numbers only, and a range that cannot reach its end, like `range(5, 1)`, is an empty list.

An `if` and `if-not` pair works inside a loop; the comma before `if-not` keeps it inside:

```thing
for every c in "abca", if c is "a", say("A"), if-not say("other").
```

```text
A
other
other
A
```

## Lesson 11: Lists

`List-of name is [ ... ]` makes a list. The dash joins the two words. Positions start at 0, and `name:2` picks position 2 (no spaces around the colon). A negative position counts from the end, so `-1` is the last. `append(numbers, 5)` adds to the end.

```thing
List-of numbers holds [0, 1, 2, 3, 4].
append(numbers, 5).
say(numbers).
say(numbers:3).
numbers:3 holds 99.
say(numbers).
say(numbers:-1).
```

```text
[0, 1, 2, 3, 4, 5]
3
[0, 1, 2, 99, 4, 5]
5
```

`numbers[3]` means the same as `numbers:3`. `List-of items.` on its own makes an empty list, and `in` asks whether something is in a list:

```thing
List-of nums holds [3, 1, 2].
say(2 in nums).
say(9 in nums).
say("ell" in "hello").
```

```text
Yes
No
Yes
```

A position also works on words: `"hello":0` is `h`. Lists can hold lists, so `grid:1:0` goes two levels in.

## Lesson 12: Books

A Book holds Entries, each with a name and a value. `Book-of name is { ... }` makes one. Look up an Entry with `name["Entry"]`, and set one the same way.

```thing
Book-of person holds {"name": "Gary", "age": 30}.
say(person["name"]).
person["email"] holds "gary@example.com".
say(person).
say("age" in person).
for every entry in person, say(entry).
```

```text
Gary
{"name": "Gary", "age": 30, "email": "gary@example.com"}
Yes
name
age
email
```

An Entry is always named with words. Asking for an Entry that is not there stops the program with an error. For every over a Book gives its Entry names.

## Lesson 13: Files

`write x in "file"` replaces a file and `add x in "file"` adds to the end of it. Each one ends with a line break. A list is written with one item per line. `read("file")` gives the whole file as words, `lines("file")` gives a list with one item per line, and `exists("file")` asks whether a file is there.

```thing
write "Ann,30" in "people.csv".
add "Bob,25" in "people.csv".
say(exists("people.csv")).
List-of rows holds lines("people.csv").
say(rows).
say(split(rows:0, ","):1).
```

```text
Yes
["Ann,30", "Bob,25"]
30
```

`split(words, separator)` cuts words into a list. A position works on anything, including the result of a call, as in the last line. Files are looked for in the folder you run the program from.

## Lesson 14: Working with data

Words that start with a capital work on a whole list:

| Write | Gives |
|---|---|
| `Total(list)` | the numbers added up |
| `Counted(list)` | how many items (also works on a Book or words) |
| `Smaller(list)` | the smallest number, or first word alphabetically |
| `Bigger(list)` | the largest, or last alphabetically |
| `Order(list)` | a new list, smallest first |
| `Order(list, backward)` | a new list, largest first |
| `Sort(list)` | puts the list itself in order |

```thing
List-of scores holds [72, 95, 88, 61, 95].
say(Total(scores)).
say(Counted(scores)).
say(Smaller(scores)).
say(Bigger(scores)).
say(Total(scores) / Counted(scores)).
say(Order(scores)).
say(Order(scores, backward)).
Sort(scores).
say(scores).
```

```text
411
5
61
95
82.2
[61, 72, 88, 95, 95]
[95, 95, 88, 72, 61]
[61, 72, 88, 95, 95]
```

`Order` leaves your list alone, and `Sort` changes it. `Total`, `Smaller` and `Bigger` also work on the values of a Book. A list that mixes numbers and words cannot be ordered, and `Total` needs real numbers, so turn words from a file into numbers with `number(...)` first.

## Lesson 15: Your own functions

A function is a recipe you name once and use many times. `Create` makes one: you write the name and the inputs in parentheses, a comma, and then the body. The body is one or more statements, and the `!` ends it. `Do` runs one, so a call to your own function never looks like a built-in such as `Total(...)`.

```thing
Create greet(name), say("Hello, {name}")!
Do greet("Ann").
Do greet("Bob").
```

```text
Hello, Ann
Hello, Bob
```

`give` hands a value back and leaves the function straight away, so a call can be used anywhere a value can:

```thing
Create area(w, h), An a holds w * h. give a!
say(Do area(3, 5)).
say("area: " plus Do area(2, 2)).
```

```text
15
area: 4
```

Here is the whole shape, with a body of two statements. The first ends with `.` and the last ends with `!`:

<!-- input: amy -->
```thing
Create getname(), A name ask("Name: "). say("Hello {name}")!
Do getname().
```

```text
Name: Hello amy
```

`give` can sit inside an `if` or a loop, and it leaves the whole function:

```thing
Create sign(n), if n is smaller-than 0, give "negative". give "zero or more"!
say(Do sign(-5)).
say(Do sign(3)).
```

```text
negative
zero or more
```

A function can call other functions or itself, and it can be written after the line that uses it:

```thing
say(Do fact(5)).
Create fact(n), if n is smaller-than 2, give 1. give n * Do fact(n - 1)!
```

```text
120
```

**Rules to remember**
- A function's inputs and any name it makes belong to that one call and disappear afterwards.
- A function can read a name from outside, but it cannot change it. `g holds 99` inside a function fails if `g` was made outside. Send results out with `give` instead.
- A function that never `give`s hands back `nil`.
- A call needs the right number of inputs, and a call that never ends stops after 200 levels.
- `Create` is written on its own, at the start of a statement. You cannot make a function inside another function or inside an `if`.
- Names like `ask`, `read`, `Total` and `say` are taken, so a function needs another name.

## Lesson 16: Using other files

`outsource "file.thing".` brings in the functions that another file makes with `Create`, so you can keep useful functions in one file and use them from many programs. Here is a small library, written in a file called `greet.thing`:

<!-- file: greet.thing -->
```thing
Create hello(name), say("Hello, {name}")!
Create twice(n), give n * 2!
say("this line is not run when the file is outsourced").
```

And a program that uses it:

```thing
outsource "greet.thing".
Do hello("Ann").
say(Do twice(21)).
```

```text
Hello, Ann
42
```

- **Only the functions come in.** Whatever else the library does at the top level, like the `say` above or a demo, is checked for mistakes but not run.
- **Names are looked for beside the file that outsources them.** A library can outsource other libraries, and outsourcing the same file twice does nothing, so files may even outsource each other.
- **Functions behave as if you had written them.** Call them with `Do`, and a function name can only be made once across all the files.
- **Errors say which file they are in:** `greet.thing, line 3: cannot divide by zero`.
- **`thing build` packs the libraries into the program**, so a built program still runs on its own after you move it away from them.

`statistics.thing` is a larger example: a port of the parts of Python's `statistics` module (mean, median, mode, variance, standard deviation, covariance and correlation). `statistics_demo.thing` outsources it and checks 23 results against Python's own module. `fractions.thing` is a port of Python's `fractions` module, written as a template, with `fractions_demo.thing` checking 32 results against Python's own. `json.thing` is a port of the decoding half of Python's `json` module, written as a template that walks through the text; `json_demo.thing` parses the 26 cases in `json_cases.txt` and gives the same results as Python's `json.loads`, including which ones are errors.

## Lesson 17: Templates

A template is a pattern for objects that hold their own values (fields) and know how to change them (methods). `template` makes one, `Do Name(...)` makes an object from it, and `Do method(...) on object` runs a method. The fields are made each time an object is made, from the template's inputs, and a lone `!` after the last method ends the template.

```thing
template Counter(start),
    A count holds start.
    Create add(amount), count holds count + amount!
    Create show(), say("Count is {count}")!
!

A c holds Do Counter(10).
Do add(5) on c.
Do show() on c.
say(count of c).
count of c holds 99.
say(c).
```

```text
Count is 15
15
Counter{count: 99}
```

- **Fields** are the `A` lines. `count of c` reads a field and `count of c holds 99` changes one.
- **Inside a method the fields are plain names.** `count holds count + amount` changes the object the method was called on, and nothing else.
- **A method can give a value back** with `give`, like any function, and can take inputs.
- **Only a name can keep a change.** `Do add(5) on c` changes `c`. Calling a method that changes the object on anything else, such as `Do add(5) on Do Counter(1)`, is an error, because the change would have nowhere to go.
- **Objects are copied, like lists.** `A d holds c` makes a separate object, and a function that is given an object gets its own copy, so it can only change the caller's data by giving a new one back.

A method can run another method of the same object with `self`:

```thing
template Account(owner, balance),
    A owner holds owner.
    A balance holds balance.
    List-of history.
    Create deposit(amount), balance holds balance + amount, append(history, amount)!
    Create withdraw(amount), if amount is bigger-than balance, give No. balance holds balance - amount. append(history, 0 - amount). give Yes!
    Create double(), Do deposit(balance) on self!
!

A acct holds Do Account("Ann", 100).
Do deposit(50) on acct.
say(Do withdraw(30) on acct).
say(Do withdraw(1000) on acct).
Do double() on acct.
say(acct).
```

```text
Yes
No
Account{owner: "Ann", balance: 240, history: [50, -30, 120]}
```

Things to know:
- A field statement that ends with `!` ends the template too, so a template with only fields can be short: `template Pair(a, b), A left holds a. A right holds b!`.
- Only fields (`A name holds value`) and `Create` methods go in a template, and a template is made at the start of a statement, not inside another one.
- An object prints as its template name and its fields, can be compared with `is`, can go in lists and Books, and works with `in`, `Counted` and `for every` (which give field names, as a Book gives Entry names).
- A method's name can be any word, since it is always called with `Do`, so `add` works as a method even though it is a word of its own elsewhere.
- Templates can be outsourced from another file, like functions.
- There is no inheritance yet.

## Lesson 18: Putting it together

Read a small data file, build a Book of ages, and work out the average. This one has no function of its own, so you could wrap it in one as practice.

```thing
write "name,age" in "people.csv".
add "bob,30" in "people.csv".
add "amy,25" in "people.csv".
List-of rows holds lines("people.csv").
Book-of ages.
for every row in rows, A parts holds split(row, ","), if parts:0 is not "name", ages[parts:0] holds number(parts:1).
say(ages).
say(Total(ages) / Counted(ages)).
```

```text
{"bob": 30, "amy": 25}
27.5
```

The `for every` line reads as one statement: for each row, split it, and when the first part is not `name`, store the age under that name.

## Quick reference

**Statements.** A statement is one or more steps joined with commas and ended with a period. A period ends any `if` or loop that the statement started. A `!` does the same and also ends the function the statement is in; it is never used anywhere else.

**Words that mean something.** `Create`, `Do`, `template`, `on`, `of`, `self`, `outsource`, `give`, `Now`, `Next`, `A`, `An`, `holds`, `say`, `if`, `if-not`, `instead`, `for every ... in`, `is`, `not`, `bigger-than`, `smaller-than`, `in`, `exists`, `and`, `or`, `plus`, `minus`, `Yes`, `No`, `nil`, `List-of`, `Book-of`, `ask`, `number`, `read`, `lines`, `split`, `round`, `fly`, `sink`, `range`, `write`, `add`, `append`, `Not`, `Total`, `Counted`, `Smaller`, `Bigger`, `Order`, `Sort`, `backward`. Your own names should avoid these.

**Capital letters.** `A`, `An`, `Create`, `Do`, `Yes`, `No`, `List-of`, `Book-of`, `Now`, `Next` and the list words (`Total`, `Order` and so on) must be written exactly. Everything else is lowercase: `is`, `is not`, `holds`, `not`, `if`, `if-not`, `instead`, `for`, `say`. `if`, `for` and `say` are also accepted with a capital letter at the start of a statement.

**One job, one spelling.** Old spellings give an error that names the new one:

| You wrote | Write instead |
|---|---|
| `x = 5`, `x equals 5`, `x is 5` | `x holds 5` |
| `Thing x holds 5` | `A x holds 5` (`An` before a vowel sound) |
| `x = y`, `x equals y`, `x Is y` | `x is y` |
| `x Is not y`, `x IsNot y` | `x is not y` |
| `x > y`, `x < y` | `x is bigger-than y`, `x is smaller-than y` |
| `IfNot ...` | `if-not ...` |
| `say x`, `say > x`, `say > value-of x` | `say(x)` |
| `List of n`, `Book of n` | `List-of n`, `Book-of n` |
| `if x: ...`, `for every c in w: ...` | `if x, ...`, `for every c in w, ...` |
| `numbers@5` | `append(numbers, 5)` |
| `write x to "f"`, `add x to "f"` | `write x in "f"`, `add x in "f"` |
| `"f" exists`, `(x) is No` | `exists("f")`, `not x` |
| `... and say "b"` after a body | end the statement with `.` and start a new one |
| `Order(s, backward=Yes)` | `Order(s, backward)` |

**Not in the language yet.** A loop that repeats until something changes, and a way to leave a loop early other than `give`. Each list you assign is copied, so changing the copy never changes the original. The first error stops the program and tells you the line.
