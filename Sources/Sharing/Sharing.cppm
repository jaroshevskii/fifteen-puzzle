export module Sharing;

// Umbrella for the Sharing library: the persistence strategy abstraction
// (`inMemory`, generic `fileStorage`) and the `Shared<T>` value wrapper. A
// concrete JSON file strategy is assembled per type in a live module (e.g.
// `AppSettingsLive`), where the JSON headers stay private to an implementation
// unit and out of this library's reachable interface.
export import :Strategy;
export import :Shared;
