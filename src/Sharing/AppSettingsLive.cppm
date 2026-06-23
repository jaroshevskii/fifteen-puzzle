export module AppSettingsLive;

import std;
import Sharing;
import AppSettings;

// Interface unit: declares the concrete JSON file strategy for `Settings`. It
// deliberately exposes no JSON types — the serializer is type-erased behind
// `PersistenceStrategy`'s closures, so the JSON library never enters this
// module's reachable interface (it lives only in the implementation unit's
// global module fragment, AppSettingsLive.cpp). That isolation is what keeps
// `import std` consumers free of the duplicate-`operator new` ambiguity that a
// C++ header in a reachable GMF would otherwise cause.
export namespace AppSettings {

Sharing::PersistenceStrategy<Settings> settingsFileStorage(std::filesystem::path path);

} // namespace AppSettings
