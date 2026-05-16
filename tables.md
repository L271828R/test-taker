# Table Rendering Test

## Basic Table

| Name       | Age | City          |
|------------|-----|---------------|
| Alice      | 30  | New York      |
| Bob        | 25  | San Francisco |
| Carol      | 35  | Chicago       |
| Dave       | 28  | Austin        |

## Programming Languages

| Language   | Paradigm        | Typing   | First Appeared |
|------------|-----------------|----------|----------------|
| C++        | Multi-paradigm  | Static   | 1985           |
| Python     | Multi-paradigm  | Dynamic  | 1991           |
| Rust       | Multi-paradigm  | Static   | 2010           |
| Haskell    | Functional      | Static   | 1990           |
| JavaScript | Multi-paradigm  | Dynamic  | 1995           |
| Go         | Procedural      | Static   | 2009           |

## Inline Formatting Inside Cells

| Feature        | Status      | Notes                          |
|----------------|-------------|--------------------------------|
| **Bold text**  | ✅ Working  | Use `**text**`                 |
| *Italic text*  | ✅ Working  | Use `*text*`                   |
| `Inline code`  | ✅ Working  | Use backticks                  |
| ~~Strikethrough~~ | ✅ Working | Use `~~text~~`              |
| [Links](https://example.com) | ✅ Working | Standard MD links |

## Data Types Reference

| Type     | Size (bytes) | Range                              | Example         |
|----------|--------------|------------------------------------|-----------------|
| `int8`   | 1            | -128 to 127                        | `int8_t x = 5`  |
| `int16`  | 2            | -32,768 to 32,767                  | `int16_t x = 5` |
| `int32`  | 4            | -2,147,483,648 to 2,147,483,647    | `int x = 5`     |
| `int64`  | 8            | -9.2×10¹⁸ to 9.2×10¹⁸             | `int64_t x = 5` |
| `float`  | 4            | ±3.4×10³⁸                          | `float x = 1.0` |
| `double` | 8            | ±1.7×10³⁰⁸                         | `double x = 1.0`|

## HTTP Status Codes

| Code | Name                  | Meaning                                      |
|------|-----------------------|----------------------------------------------|
| 200  | OK                    | Request succeeded                            |
| 201  | Created               | Resource created successfully                |
| 301  | Moved Permanently     | Resource has a new permanent URL             |
| 400  | Bad Request           | Server cannot process the request            |
| 401  | Unauthorized          | Authentication is required                   |
| 403  | Forbidden             | Server refuses to authorise the request      |
| 404  | Not Found             | Resource could not be located                |
| 500  | Internal Server Error | Unexpected server-side condition             |
| 503  | Service Unavailable   | Server is temporarily unable to handle it    |

## Single-Column Table

| Fruit      |
|------------|
| Apple      |
| Banana     |
| Cherry     |
| Dragonfruit|

## Two-Column Table

| Key         | Value              |
|-------------|--------------------|
| OS          | macOS Sequoia      |
| Compiler    | AppleClang 17      |
| wxWidgets   | 3.3.2              |
| Mermaid     | 10.x               |
| Build tool  | CMake 4.x          |
