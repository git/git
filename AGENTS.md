# Architecture Overview

This document provides a high-level architecture overview of the system, showing how different services and components interact.

## System Architecture

```mermaid
graph TB
    subgraph "Documentation Layer"
        Docs["📚 Docs Repository<br/>(TypeScript/SCSS)"]
        Hextra["🔯 Hextra Theme<br/>(HTML/JS/CSS)"]
    end

    subgraph "Web & Frontend"
        Web["🌐 Web Monorepo<br/>(MDX/TypeScript)"]
        Markup["Markup Validator<br/>(HTML/Perl)"]
    end

    subgraph "Core Services"
        Git["Git Source<br/>(C/Shell/Perl)"]
        Engine["SE Engine<br/>(Critical Position)"]
    end

    subgraph "Tools & Utilities"
        Transfer["Termination of Transfer<br/>(JavaScript/PHP)"]
        Home["Home Configuration<br/>(Metadata)"]
    end

    subgraph "Infrastructure"
        GitGadget["GitGitGadget<br/>(Patch Management)"]
    end

    %% Relationships
    Docs -->|uses| Hextra
    Docs -->|documents| Git
    Web -->|references| Docs
    Markup -->|validates| Web
    Git -->|manages| GitGadget
    Engine -->|powers| Web
    Transfer -->|tools| Home
    Hextra -->|theme| Web

    %% Styling
    classDef core fill:#ff6b6b,stroke:#c92a2a,color:#fff
    classDef frontend fill:#4dabf7,stroke:#1971c2,color:#fff
    classDef tools fill:#51cf66,stroke:#2b8a3e,color:#fff
    classDef docs fill:#a78bfa,stroke:#6d28d9,color:#fff
    
    class Git,Engine core
    class Web,Markup frontend
    class Transfer,Home tools
    class Docs,Hextra docs
```

## Component Details

### Core Services
- **Git Repository**: The primary source code repository using C (50.1%), Shell (38.9%), and Perl (4.3%)
- **SE Engine**: Critical service that powers the main application functionality

### Frontend & Documentation
- **Web Monorepo**: Main web application (79.7% MDX, 17.5% TypeScript)
- **Docs**: Comprehensive documentation site (97.2% TypeScript)
- **Hextra Theme**: Modern Hugo theme for beautiful documentation (HTML, JavaScript, CSS)
- **Markup Validator**: HTML/Markup validation service

### Tools & Utilities
- **Termination of Transfer**: Transfer management tool (JavaScript, PHP)
- **Home Configuration**: System configuration and metadata

### Infrastructure
- **GitGitGadget**: Patch management and review system for git submissions

## Technology Stack

| Layer | Primary Technologies |
|-------|----------------------|
| Frontend | TypeScript, MDX, HTML, CSS, JavaScript |
| Backend | C, Shell, Perl, PHP |
| Documentation | TypeScript, MDX, HTML |
| Scripting | Python, Tcl, Perl |
| Build System | Makefile, Shell |

## Data Flow

1. **Development** → Git Repository (managed via GitGitGadget)
2. **Source Code** → SE Engine (processes core logic)
3. **Output** → Web Monorepo (frontend rendering)
4. **Validation** → Markup Validator (quality assurance)
5. **Documentation** → Docs + Hextra Theme (user-facing docs)
6. **Operations** → Transfer Tool + Home Config (system management)
