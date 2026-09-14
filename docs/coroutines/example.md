# Coroutines — Examples

Based on `test/coroutines/main.cpp`.

```cpp
#include <acrux/coroutines/Coroutine.h>
#include <acrux/coroutines/CoroutineRunner.h>
#include <acrux/coroutines/standard/WaitConditions.h>

using namespace acrux::coroutines;
using namespace acrux::coroutines::standard;

struct Character
{
    int hp = 100;

    Coroutine attack(Character& target)
    {
        print("Attacking...\n");
        co_yield waitForSeconds(1.0f);        // waits one real-time second
        target.hp -= 20;
        co_yield nullptr;                     // waits exactly one frame
    }

    Coroutine battleSequence(Character& enemy)
    {
        co_yield attack(enemy);               // starts a child Coroutine and waits
                                               // for it to finish

        co_yield waitUntil([&]{ return enemy.hp <= 0; }); // waits on a predicate
    }
};

int main()
{
    CoroutineRunner runner;                   // becomes the active runner

    Character hero, enemy;
    CoroutineHandle battle = startCoroutine(hero.battleSequence(enemy));

    while (battle.isRunning())
    {
        runner.update();                      // called once per frame/tick
    }
}
```

## Yield expressions

```cpp
co_yield nullptr;                              // resume next frame
co_yield waitForSeconds(2.5f);                 // resume after a real-time delay
co_yield waitUntil([this]{ return ready; });   // resume once the predicate is true
co_yield waitWhile([this]{ return loading; }); // resume once the predicate is false
co_yield someOtherCoroutine();                 // start a coroutine and wait for it to finish
co_yield waitFor(handleStartedEarlier);        // wait for a coroutine already in progress
```

`waitForSeconds`/`waitUntil`/`waitWhile`/`waitFor` are declared in `standard/WaitConditions.h`, in the `acrux::coroutines::standard` namespace. They are not part of the core module — see `architecture.md` for what that means in practice, and how to write an equivalent helper of your own.

## Starting a coroutine without waiting on it immediately

```cpp
CoroutineHandle bg = startCoroutine(controller.fadeOut(delta)); // starts immediately
// ... other work, possibly across several yields ...
co_yield waitFor(bg); // waits on it later, if needed
```

## Stopping a coroutine

```cpp
CoroutineHandle h = startCoroutine(enemy.patrol());
...
stopCoroutine(h); // h becomes INVALID_COROUTINE_HANDLE; safe to call again
// equivalently:
h.stop();
```

## Attaching extra data to a coroutine

`CoroutineRunner` can be derived from to associate extra data with each coroutine it starts — for example, which entity a coroutine belongs to in a game engine. `CoroutineHandle` is hashable, so it can be used directly as a key. Children started by a coroutine — whether through `co_yield childTask()` or a direct `startCoroutine()` call from inside its body — can inherit the same data automatically, using the `parent` argument to `onCoroutineStarted()`.

```cpp
#include <unordered_map>

using EntityId = int;

class GameCoroutineRunner : public CoroutineRunner
{
public:
    // Without this, declaring startCoroutine(task, owner) below would hide
    // the base class's startCoroutine(task) entirely.
    using CoroutineRunner::startCoroutine;

    CoroutineHandle startCoroutine(Coroutine task, EntityId owner)
    {
        CoroutineHandle handle = CoroutineRunner::startCoroutine(std::move(task));
        if (handle) m_owners[handle] = owner;
        return handle;
    }

    EntityId ownerOf(CoroutineHandle handle) const
    {
        auto it = m_owners.find(handle);
        return it != m_owners.end() ? it->second : -1;
    }

protected:
    // If `parent` is a coroutine we're already tracking an owner for,
    // inherit it -- this is what makes a coroutine's children automatically
    // belong to the same entity, without either call site passing an
    // owner explicitly. The explicit-owner startCoroutine() overload above
    // still runs its own assignment afterward, so an explicit owner always
    // takes priority over an inherited one.
    void onCoroutineStarted(CoroutineHandle handle, CoroutineHandle parent) override
    {
        auto it = m_owners.find(parent);
        if (it != m_owners.end())
        {
            m_owners[handle] = it->second;
        }
    }

    // Called whenever a tracked coroutine finishes, however it finished --
    // keeps m_owners from accumulating stale entries.
    void onCoroutineFinished(CoroutineHandle handle) override
    {
        m_owners.erase(handle);
    }

private:
    std::unordered_map<CoroutineHandle, EntityId> m_owners;
};
```

```cpp
GameCoroutineRunner runner;
CoroutineHandle h = runner.startCoroutine(enemy.patrol(), enemy.id());
// ...
EntityId owner = runner.ownerOf(h);
```

With this in place, a coroutine spawning its own sub-coroutines needs no special handling at all — they simply belong to the same entity:

```cpp
Coroutine Enemy::patrol()
{
    // co_yield of a child: inherits this coroutine's owner automatically.
    co_yield lookAround();

    // a fire-and-forget child, started directly: also inherits it.
    startCoroutine(playFootstepSounds());

    co_yield waitForSeconds(1.0f);
}
```

This pattern is not limited to a single piece of data. A derived class can store anything it needs — a full struct, a pointer back to a subsystem, several parallel maps — since `CoroutineRunner` places no constraints on what "extra data" means, or on how `onCoroutineStarted()` chooses to propagate it.
