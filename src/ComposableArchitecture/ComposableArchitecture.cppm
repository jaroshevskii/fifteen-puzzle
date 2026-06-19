export module ComposableArchitecture;

// Re-exports the core building blocks (Effect, Reduce/Scope/combine, Store,
// TestStore, CasePath) and the Dependencies library, mirroring how the Swift
// package surfaces `Dependencies` from `ComposableArchitecture`.
export import :CasePath;
export import :Effect;
export import :Reducer;
export import :Store;
export import :TestStore;
export import Dependencies;
