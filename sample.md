# Sample Markdown with Mermaid

A quick tour of **MDViewer** — bold, *italic*, `inline code`, and ~~strikethrough~~.

---

## Flowchart

```mermaid
flowchart TD
    A([Start]) --> B{User logs in?}
    B -- Yes --> C[Load dashboard]
    B -- No  --> D[Show login page]
    D --> E[Enter credentials]
    E --> B
    C --> F([End])
```

## Sequence Diagram

```mermaid
sequenceDiagram
    participant Browser
    participant Server
    participant DB

    Browser->>Server: GET /api/data
    Server->>DB: SELECT * FROM items
    DB-->>Server: rows
    Server-->>Browser: JSON response
```

## Class Diagram

```mermaid
classDiagram
    class Animal {
        +String name
        +int age
        +speak() void
    }
    class Dog {
        +fetch() void
    }
    class Cat {
        +purr() void
    }
    Animal <|-- Dog
    Animal <|-- Cat
```

## Gantt Chart

```mermaid
gantt
    title Project Timeline
    dateFormat  YYYY-MM-DD
    section Design
    Wireframes       :done,    d1, 2024-01-01, 7d
    Mockups          :done,    d2, after d1,   5d
    section Development
    Backend API      :active,  e1, 2024-01-15, 10d
    Frontend UI      :         e2, after e1,   8d
    section Testing
    QA               :         t1, after e2,   5d
```

## Pie Chart

```mermaid
pie title Browser Market Share
    "Chrome"  : 65
    "Safari"  : 19
    "Firefox" : 4
    "Edge"    : 4
    "Other"   : 8
```

---

## Code Block (non-Mermaid)

```python
def fibonacci(n: int) -> int:
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

print(fibonacci(10))  # 55
```

## Table

| Diagram type  | Keyword        | Supported |
|---------------|----------------|-----------|
| Flowchart     | `flowchart`    | ✅        |
| Sequence      | `sequenceDiagram` | ✅     |
| Class         | `classDiagram` | ✅        |
| Gantt         | `gantt`        | ✅        |
| Pie           | `pie`          | ✅        |

> **Tip:** Click any diagram to open the full-screen zoom view.  
> Scroll to zoom · drag to pan · ESC to close.
