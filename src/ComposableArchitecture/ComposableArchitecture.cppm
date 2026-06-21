export module ComposableArchitecture;

// Re-exports the TCA 2.0-style building blocks — Feature/Update, the Store
// runtime, Scope, CasePath, TestStore — and the Dependencies library, mirroring
// how the Swift package surfaces `Dependencies` from `ComposableArchitecture`.
export import :CasePath;
export import :Store;
export import :Feature;
export import :Runtime;
export import :Scope;
export import :TestStore;
export import Dependencies;
