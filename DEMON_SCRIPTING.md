# DemonScript Guide

DemonScript is the lightweight behavior language used by DemonEngine runtime objects. It is designed for fast gameplay-style transform animation, simple state, and hot-reloaded scene behaviors.

## File Shape

```demonscript
behavior ObjectRotator {
  prop rotationSpeed: float = 90.0

  on_spawn {
    print("ObjectRotator spawned")
  }

  on_update(deltaTime: float) {
    transform.rotation.y += rotationSpeed * dt
  }
}
```

## Properties

Properties are exposed on the script component and are saved with the scene.

```demonscript
prop enabled: bool = true
prop score: int = 0
prop speed: float = 4.0
prop displayName: string = "Hero"
prop offset: vec3 = {0, 1, 0}
hidden internalPhase: float = 0.0
```

Supported types: `bool`, `int`, `float`, `str`/`string`, and `vec3`.

## Events

`on_spawn` runs when the behavior starts.

`on_update(deltaTime: float)` runs each runtime frame. Use `deltaTime` or the short alias `dt`.

`on_trigger` runs for trigger-style payloads. Use `other` for the payload when available.

`on_signal` runs for string signal payloads. Use `signal` for the payload.

## Variables

Local variables are declared with `var`. They live only for the current event call.

```demonscript
on_update(deltaTime: float) {
  var turn = 120.0 * dt
  var bobOffset: float = bob(8.0, 0.08)
  transform.rotation.y += turn
  transform.position.y = 1.7 + bobOffset
}
```

## Transform Targets

Use these targets to move, rotate, and scale an object:

```demonscript
transform.position.x
transform.position.y
transform.position.z
transform.translation.x
transform.translation.y
transform.translation.z
transform.rotation.x
transform.rotation.y
transform.rotation.z
transform.scale.x
transform.scale.y
transform.scale.z
```

## Built-In Numeric Functions

Math: `sin`, `cos`, `tan`, `abs`, `sqrt`, `floor`, `ceil`, `round`, `pow`, `min`, `max`, `clamp`.

Angles: `deg2rad`, `rad2deg`.

Animation helpers: `lerp`, `inverseLerp`, `smoothstep`, `repeat`, `pingpong`, `wave`, `bob`, `pulse`.

Time values: `time` or `runtime.time` for scene runtime seconds, `dt` or `deltaTime` inside `on_update`.

## Physics Functions

Physics write functions are standalone statements. They create the needed `RigidBody` or `BoxCollider` component when it is missing.

```demonscript
physics_set_velocity(x, y, z)
physics_add_velocity(x, y, z)
physics_add_force(x, y, z)
physics_add_impulse(x, y, z)
physics_clear_velocity()
physics_set_mass(mass)
physics_set_gravity(enabled)
physics_set_gravity_scale(scale)
physics_set_kinematic(enabled)
physics_set_trigger(enabled)
physics_set_box(halfX, halfY, halfZ)
physics_set_collider_offset(x, y, z)
physics_set_friction(value)
physics_set_restitution(value)
```

Physics read helpers can be used inside numeric expressions:

```demonscript
physics_velocity_x()
physics_velocity_y()
physics_velocity_z()
physics_speed()
physics_grounded()
```

## Examples

Camera bob:

```demonscript
behavior CameraBob {
  prop eyeHeight: float = 1.7
  prop bobSpeed: float = 8.0
  prop bobAmount: float = 0.06

  on_update(deltaTime: float) {
    transform.position.y = eyeHeight + bob(bobSpeed, bobAmount)
  }
}
```

Playable jump arc or jump-pad style bounce:

```demonscript
behavior JumpPadMotion {
  prop baseHeight: float = 0.0
  prop jumpHeight: float = 1.2
  prop jumpSpeed: float = 2.4

  on_update(deltaTime: float) {
    transform.position.y = baseHeight + pingpong(time * jumpSpeed, jumpHeight)
  }
}
```

Mesh rotation:

```demonscript
behavior MeshSpinner {
  prop yawSpeed: float = 90.0
  prop pitchWave: float = 10.0

  on_update(deltaTime: float) {
    transform.rotation.y += yawSpeed * dt
    transform.rotation.x = wave(2.0, pitchWave)
  }
}
```

Move animation:

```demonscript
behavior SideToSideMove {
  prop startX: float = 0.0
  prop distance: float = 4.0
  prop speed: float = 1.5

  on_update(deltaTime: float) {
    transform.position.x = startX + wave(speed, distance)
  }
}
```

Scale pulse:

```demonscript
behavior ScalePulse {
  prop pulseSpeed: float = 4.0

  on_update(deltaTime: float) {
    var s = pulse(pulseSpeed, 0.8, 1.25)
    transform.scale.x = s
    transform.scale.y = s
    transform.scale.z = s
  }
}
```

Smooth one-way lift:

```demonscript
behavior SmoothLift {
  prop startY: float = 0.0
  prop endY: float = 5.0
  prop duration: float = 3.0

  on_update(deltaTime: float) {
    var t = clamp(time / duration, 0.0, 1.0)
    transform.position.y = lerp(startY, endY, smoothstep(0.0, 1.0, t))
  }
}
```

Physics-controlled jump:

```demonscript
behavior PhysicsJump {
  prop jumpImpulse: float = 7.5

  on_spawn {
    physics_set_mass(80.0)
    physics_set_box(0.35, 0.9, 0.35)
    physics_set_collider_offset(0.0, -0.9, 0.0)
  }

  on_signal {
    if (physics_grounded()) {
      physics_add_impulse(0.0, jumpImpulse, 0.0)
    }
  }
}
```
