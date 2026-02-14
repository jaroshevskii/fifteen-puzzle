# 15 Puzzle

A implementation of the classic 15 Puzzle built with C++ and raylib.  
The project focuses on clarity, deterministic layout, and straightforward rendering logic.

<img width="488" height="520" alt="image" src="https://github.com/user-attachments/assets/8b4296a7-e054-4c21-8e27-a9fba1c9a2e4" />

# Demo

https://github.com/user-attachments/assets/014517ab-ebbd-4619-8409-10c19ffa2edd

## Overview

This project implements:

- 4×4 sliding puzzle grid  
- Mouse interaction  
- Shuffle logic  
- Win state detection  
- Simple rendering loop  
- Optional FPS display  

Architecture is intentionally simple:  
- `Card` — UI + state  
- Stateless helper functions for puzzle logic  
- Single render loop  

## Controls

- Move tile — Left mouse click  
- Shuffle — S  
- Restart - R
- Restart after victory — Mouse click  

## Rendering Model

The game uses a standard game loop: input → state mutation → draw → repeat  

## Game Logic

- The empty tile is represented by ""  
- Movement is allowed only for adjacent tiles  
- Victory condition: tiles 1–15 are ordered and the empty tile is last  

## Design Notes

- No global mutation outside the main loop  
- Rectangle-based layout  
- No physics, no animation  
- Deterministic board generation  
- Lightweight dependency surface  

## Possible Improvements

- Solvability validation for shuffle  
- Move counter  
- Timer  
- Animation transitions  
- Adaptive layout  
- State isolation (MVC-like separation)  

## License

This project is licensed under the MIT License. See the [LICENSE](./LICENSE.md) file for details.
