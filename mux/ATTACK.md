---
author: Brazil
date: June 2020
title: ATTACK
---

You should familiarize yourself with defensive procedures that serve to
thwart the effects of attacks. Do not wait for an attack before learning
how to defend against it. Practice for this eventuality now.

# Player Creation:

If you are running an open game, it is possible for an automated script
to create players faster than you can can destroy them. Even if a group
of people are doing it manually, the odds are they can create player
objects faster than you can destroy them.

Wizards can remedy this with:

```
    @disable logins
```

In addition, the #1 Wizard can remedy this with:

```
    @admin register_site=0/0
```

# `@aahear` attack:

With TinyMUX 2.2 and later, the `@aahear` exploit is no longer as useful
to attackers as it once was. Attackers would typically mix this attack
with other forms of attack or spam to obscure it.

# `@pemit/page` attack:

The combination of `lwho()` and `@pemit` can be used for spamming.

As a player, `WIZARD`, or `#1`, you can prevent a spam attack with:

```
    &CANPAGEME me=0
    @lock/page me=CANPAGEME/1
    @admin pemit_far_players=0   (the default)
```

# Attack on the queue:

Usually, queueing commands will drain an attacker's store of coins.
However, if they happen to obtain the free_money power (or `WIZARD`
permissions ... by which time you are toast anyway), then they can
attack the queue.

As a `WIZARD` or `#1`, you can remedy this attack with:

```
    @disable dequeueing
```

# Attribute attack:

The server will not allow more than a few thousand attributes per
object. This attack is no longer possible.

# Attribute Name Attack:

Attribute names are different than the attribute values they name. By
default, mortals are not allowed to create more than 5000 new names per
hour. See '`wizhelp user_attrib_per_hour`'. However, the site admin
still needs to keep an eye on attribute names as described below in
'General Server Hygiene'.

# `@Mail` Attack:

Mortals are not allowed to send more than 50 `@mail`s per hour. However,
the site admin still needs to keep an eye on the size of `@mail`. See
'`wizhelp mail_per_hour`'.

# CPU Slaming:

There are ways of consuming hours and days of CPU time with carefully
chosen softcode. The `lag_limit` configuration option controls the point
at which the server abbreviates its efforts. In some ways, this is like
hitting a Function Invocation Limit. One other thing that hitting a
`lag_limit` does is `@halt` the offending code.

# General server hygiene:

There are a few things you should do on a regular basis:

-   Backups. Off-line backups. Do not expect much sympathy if you do not
    have a good backup.

-   Good backups. Manually verify that your backups are valid and
    complete. Do not expect much sympathy if you do not have a good
    backup. Did you move your backup offline? Can you still access it?

-   Backup often. How much work are you willing to lose?

-   Keep an eye on how much CPU and memory your server process is
    consuming. Your memory usage will gradually increase and slowly
    approach some number. If your memory usage suddenly goes above this,
    something is wrong. If your CPU usage is not usually 0% or on a busy
    game, less than 5% for awhile, Look for an attack in progress. Get a
    'feel' for what is normal for your game and be sensitive to what
    might be abnormal.

```
        ps ux -A
```

-   Keep an eye on attribute names (Vattr names) with the command below.
    The column you care about is the 'entries' column. This number will
    always increase; however, stale names can be removed by `#1` using the
	`@dbclean` command. Run `@dbclean` every 3 months.

```
        @list hashstats
```
