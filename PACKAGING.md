# Installing CoffeeRDP

CoffeeRDP is distributed as an RPM for Fedora via
[Copr](https://copr.fedorainfracloud.org/), Fedora's community package
repository. Copr isn't enabled by default, so there's one extra step
before `dnf install` works.

## Install

```sh
sudo dnf copr enable codeneedscoffee/CoffeeRDP
sudo dnf install coffeerdp
```

Case matters on the first command: Copr project names aren't normalized, and
`codeneedscoffee/coffeerdp` (all lowercase) 404s.

The first command adds the repository; the second installs `coffeerdp`
(the connection manager, with the desktop launcher) along with
`coffee-rdp-session` and `coffee-rdp-auth`, the two helper binaries it
launches per connection. Everything else the package needs (SDL3, GTK4,
libadwaita, WebKitGTK, FreeRDP 3, libportal-gtk4) is already in Fedora's
own repositories, so nothing beyond enabling the Copr repo is required.

## Finding it

After installing, CoffeeRDP shows up in your application launcher (GNOME
Activities, GNOME Software, or equivalent) as **CoffeeRDP**. If it doesn't
appear right away, refresh the desktop's app cache:

```sh
appstreamcli refresh --force
```

## Updating

Once the Copr repo is enabled, `coffeerdp` updates through your normal
`dnf upgrade` like any other package, no extra steps.

## Uninstalling

```sh
sudo dnf remove coffeerdp
sudo dnf copr disable codeneedscoffee/CoffeeRDP
```

The second command is only needed if you also want to stop tracking the
repository itself.

## Getting help

Source code, issue tracker, and everything else: see the
[GitHub repository](https://github.com/CodeNeedsCoffee/CoffeeRDP).
