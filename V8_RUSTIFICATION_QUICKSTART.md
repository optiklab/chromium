# Quick Start Guide: V8 Parser Rustification

> **Getting Started** with the recommended Hybrid Parser Replacement approach

For background and full analysis, see:
- [Executive Summary](./V8_RUSTIFICATION_SUMMARY.md)
- [Full Research Document](./V8_RUSTIFICATION_RESEARCH.md)

---

## Prerequisites

### Required Knowledge
- ✅ C++ (especially modern C++17/20)
- ✅ Rust (intermediate level)
- ✅ Compiler design basics (lexing, parsing, AST)
- ✅ V8 architecture overview
- ⚠️ FFI/interop patterns
- ⚠️ Build systems (GN, Cargo)

### Required Tools
- Rust toolchain (rustc, cargo)
- Chromium development environment
- rust-gdb or rust-lldb
- CXX bridge library

---

## Learning Path (Before Starting)

### Week 1-2: V8 Architecture
**Goal:** Understand how V8 works and integrates with Chromium

1. **Read Key Documents** (Start here!)
   ```bash
   # In Chromium repository
   cd /path/to/chromium
   
   # V8 Bindings
   less third_party/blink/renderer/bindings/core/v8/V8BindingDesign.md
   
   # Content Layer
   less content/README.md
   
   # Gin (V8 Wrapper)
   less gin/README.md
   ```

2. **Watch V8 Overview**
   - V8 Dev Summit talks: https://v8.dev/
   - "How V8 Works" presentations

3. **Study Parser Code**
   ```bash
   # Locate V8 parser source (if available in your checkout)
   find . -name "parser.cc" -o -name "parser.h"
   
   # Study key classes:
   # - v8::internal::Parser
   # - v8::internal::Scanner
   # - v8::internal::AstNode
   ```

### Week 3-4: Rust in Chromium
**Goal:** Understand how Rust integrates with Chromium

1. **Read Rust Documentation**
   ```bash
   cd /path/to/chromium
   
   # FFI Guidelines
   cat docs/rust/ffi.md
   
   # API Design
   cat docs/rust/api_design.md
   
   # Style Guide
   cat styleguide/rust/rust.md
   ```

2. **Explore Existing Rust**
   ```bash
   # Check existing Rust crates
   ls third_party/rust/
   
   # Find Rust usage examples
   find . -name "*.rs" | head -20
   
   # Study BUILD.gn files with Rust targets
   find . -name "BUILD.gn" -exec grep -l "rust" {} \;
   ```

3. **Learn CXX Bridge**
   - Official Guide: https://cxx.rs/
   - Tutorial: https://cxx.rs/tutorial.html
   - Examples: https://github.com/dtolnay/cxx/tree/master/demo

### Week 5-6: Hands-On Practice
**Goal:** Get comfortable with FFI and tooling

1. **Simple FFI Example**
   ```rust
   // practice/lib.rs
   #[cxx::bridge]
   mod ffi {
       extern "Rust" {
           fn hello_from_rust() -> String;
       }
   }
   
   fn hello_from_rust() -> String {
       "Hello from Rust!".to_string()
   }
   ```
   
   ```cpp
   // practice/main.cc
   #include "practice/lib.rs.h"
   #include <iostream>
   
   int main() {
       auto msg = hello_from_rust();
       std::cout << msg << std::endl;
       return 0;
   }
   ```

2. **Build & Debug Practice**
   ```bash
   # Set up Rust debugging
   export RUST_BACKTRACE=1
   export RUST_LOG=debug
   
   # Use rust-gdb
   rust-gdb ./your_binary
   ```

3. **Read V8 Parser Code**
   - Understand token types
   - Follow parsing flow for simple function
   - Map recursive descent parsing pattern

---

## Project Setup

### Directory Structure
```
chromium/
├── third_party/
│   └── rust_v8_parser/        # New Rust parser crate
│       ├── Cargo.toml
│       ├── src/
│       │   ├── lib.rs         # Main library
│       │   ├── scanner.rs     # Tokenizer
│       │   ├── parser.rs      # Parser logic
│       │   ├── ast.rs         # AST types
│       │   └── ffi.rs         # FFI bridge
│       └── BUILD.gn           # Build configuration
│
└── v8/                        # V8 source (if in tree)
    └── src/
        └── parsing/
            ├── parser.cc      # Original C++ parser
            └── rust_parser_adapter.cc  # FFI adapter
```

### Initial Setup Steps

1. **Create Rust Crate**
   ```bash
   cd /path/to/chromium/third_party
   mkdir rust_v8_parser
   cd rust_v8_parser
   cargo init --lib
   ```

2. **Configure Cargo.toml**
   ```toml
   [package]
   name = "rust_v8_parser"
   version = "0.1.0"
   edition = "2021"
   
   [dependencies]
   cxx = "1.0"
   
   [build-dependencies]
   cxx-build = "1.0"
   
   [lib]
   crate-type = ["staticlib"]
   ```

3. **Create BUILD.gn**
   ```gn
   # BUILD.gn
   import("//build/rust/rust_static_library.gni")
   
   rust_static_library("rust_v8_parser") {
     crate_root = "src/lib.rs"
     sources = [
       "src/lib.rs",
       "src/scanner.rs",
       "src/parser.rs",
       "src/ast.rs",
       "src/ffi.rs",
     ]
     deps = [
       "//third_party/rust/cxx/v1:lib",
     ]
   }
   ```

---

## Implementation Phases

### Phase 1: Tokenizer/Scanner (Weeks 1-4)

**Goal:** Rust tokenizer that can scan JavaScript source

1. **Define Token Types**
   ```rust
   // src/scanner.rs
   #[derive(Debug, Clone, PartialEq)]
   pub enum TokenType {
       // Keywords
       Function,
       Return,
       If,
       Else,
       // ... more keywords
       
       // Literals
       Identifier(String),
       Number(f64),
       String(String),
       
       // Operators
       Plus,
       Minus,
       Star,
       // ... more operators
       
       // Special
       EOF,
   }
   
   #[derive(Debug, Clone)]
   pub struct Token {
       pub token_type: TokenType,
       pub line: usize,
       pub column: usize,
   }
   ```

2. **Implement Scanner**
   ```rust
   pub struct Scanner<'a> {
       source: &'a [u8],
       position: usize,
       line: usize,
       column: usize,
   }
   
   impl<'a> Scanner<'a> {
       pub fn new(source: &'a str) -> Self {
           Self {
               source: source.as_bytes(),
               position: 0,
               line: 1,
               column: 1,
           }
       }
       
       pub fn next_token(&mut self) -> Result<Token, ScanError> {
           self.skip_whitespace();
           
           if self.is_at_end() {
               return Ok(Token {
                   token_type: TokenType::EOF,
                   line: self.line,
                   column: self.column,
               });
           }
           
           // Token recognition logic
           // This is where Rust's safety prevents buffer overruns!
           match self.peek() {
               Some(b'0'..=b'9') => self.scan_number(),
               Some(b'a'..=b'z') | Some(b'A'..=b'Z') => self.scan_identifier(),
               Some(b'"') => self.scan_string(),
               _ => Err(ScanError::UnexpectedCharacter),
           }
       }
       
       fn peek(&self) -> Option<u8> {
           self.source.get(self.position).copied()
       }
       
       fn advance(&mut self) -> Option<u8> {
           let ch = self.peek()?;
           self.position += 1;
           self.column += 1;
           Some(ch)
       }
       
       fn is_at_end(&self) -> bool {
           self.position >= self.source.len()
       }
   }
   ```

3. **Add Tests**
   ```rust
   #[cfg(test)]
   mod tests {
       use super::*;
       
       #[test]
       fn test_simple_tokens() {
           let source = "function foo() { return 42; }";
           let mut scanner = Scanner::new(source);
           
           let tokens: Vec<Token> = scanner
               .by_ref()
               .collect::<Result<_, _>>()
               .unwrap();
           
           assert_eq!(tokens[0].token_type, TokenType::Function);
           assert_eq!(tokens[1].token_type, TokenType::Identifier("foo".into()));
           // ... more assertions
       }
       
       #[test]
       fn test_string_literal() {
           let source = r#""hello world""#;
           let mut scanner = Scanner::new(source);
           let token = scanner.next_token().unwrap();
           
           assert_eq!(
               token.token_type,
               TokenType::String("hello world".into())
           );
       }
   }
   ```

### Phase 2: Parser Core (Weeks 5-10)

**Goal:** Basic recursive descent parser

1. **Define AST Types**
   ```rust
   // src/ast.rs
   pub enum Expression {
       Literal(Literal),
       Identifier(String),
       Binary {
           left: Box<Expression>,
           operator: BinaryOp,
           right: Box<Expression>,
       },
       Call {
           callee: Box<Expression>,
           arguments: Vec<Expression>,
       },
       Function(FunctionLiteral),
   }
   
   pub struct FunctionLiteral {
       pub name: Option<String>,
       pub parameters: Vec<String>,
       pub body: Vec<Statement>,
   }
   
   pub enum Statement {
       Expression(Expression),
       Return(Option<Expression>),
       If {
           condition: Expression,
           then_branch: Box<Statement>,
           else_branch: Option<Box<Statement>>,
       },
       Block(Vec<Statement>),
   }
   ```

2. **Implement Parser**
   ```rust
   // src/parser.rs
   pub struct Parser<'a> {
       scanner: Scanner<'a>,
       current_token: Token,
   }
   
   impl<'a> Parser<'a> {
       pub fn new(source: &'a str) -> Result<Self, ParseError> {
           let mut scanner = Scanner::new(source);
           let current_token = scanner.next_token()?;
           Ok(Self {
               scanner,
               current_token,
           })
       }
       
       pub fn parse_function_literal(&mut self) -> Result<FunctionLiteral, ParseError> {
           // This is the critical function that was crashing!
           // Rust's safety prevents the access violations
           
           self.expect(TokenType::Function)?;
           
           let name = if self.current_token.token_type.is_identifier() {
               Some(self.consume_identifier()?)
           } else {
               None
           };
           
           self.expect(TokenType::LeftParen)?;
           let parameters = self.parse_parameter_list()?;
           self.expect(TokenType::RightParen)?;
           
           self.expect(TokenType::LeftBrace)?;
           let body = self.parse_block()?;
           self.expect(TokenType::RightBrace)?;
           
           Ok(FunctionLiteral {
               name,
               parameters,
               body,
           })
       }
       
       fn expect(&mut self, expected: TokenType) -> Result<(), ParseError> {
           if self.current_token.token_type == expected {
               self.advance()?;
               Ok(())
           } else {
               Err(ParseError::UnexpectedToken {
                   expected,
                   found: self.current_token.clone(),
               })
           }
       }
       
       fn advance(&mut self) -> Result<(), ParseError> {
           self.current_token = self.scanner.next_token()?;
           Ok(())
       }
   }
   ```

### Phase 3: FFI Bridge (Weeks 11-14)

**Goal:** Connect Rust parser to C++ V8

1. **Define FFI Interface**
   ```rust
   // src/ffi.rs
   #[cxx::bridge]
   mod ffi {
       // Shared types
       struct ParseResult {
           success: bool,
           error_message: String,
       }
       
       // Rust functions called from C++
       extern "Rust" {
           fn rust_parse_function(source: &str) -> ParseResult;
           fn rust_scanner_next_token(source: &str, position: usize) -> Token;
       }
       
       // C++ functions called from Rust
       unsafe extern "C++" {
           include!("v8/src/ast/ast.h");
           
           type AstNode;
           fn CreateFunctionNode() -> UniquePtr<AstNode>;
       }
   }
   
   pub fn rust_parse_function(source: &str) -> ParseResult {
       match Parser::new(source) {
           Ok(mut parser) => match parser.parse_function_literal() {
               Ok(func) => ParseResult {
                   success: true,
                   error_message: String::new(),
               },
               Err(e) => ParseResult {
                   success: false,
                   error_message: format!("{:?}", e),
               },
           },
           Err(e) => ParseResult {
               success: false,
               error_message: format!("{:?}", e),
           },
       }
   }
   ```

2. **C++ Adapter**
   ```cpp
   // v8/src/parsing/rust_parser_adapter.cc
   #include "third_party/rust_v8_parser/src/ffi.rs.h"
   #include "src/ast/ast.h"
   
   namespace v8::internal {
   
   class RustParserAdapter {
    public:
     bool ParseFunction(const char* source) {
       auto result = rust_parse_function(source);
       if (!result.success) {
         ReportError(result.error_message);
         return false;
       }
       return true;
     }
     
    private:
     void ReportError(const std::string& message) {
       // Integrate with V8's error reporting
     }
   };
   
   }  // namespace v8::internal
   ```

### Phase 4: Testing & Integration (Weeks 15-20)

**Goal:** Validate correctness and performance

1. **Unit Tests**
   ```rust
   #[cfg(test)]
   mod tests {
       use super::*;
       
       #[test]
       fn test_parse_simple_function() {
           let source = "function add(a, b) { return a + b; }";
           let result = rust_parse_function(source);
           assert!(result.success);
       }
       
       #[test]
       fn test_parse_nested_function() {
           let source = r#"
               function outer() {
                   function inner() {
                       return 42;
                   }
                   return inner();
               }
           "#;
           let result = rust_parse_function(source);
           assert!(result.success);
       }
       
       #[test]
       fn test_error_handling() {
           let source = "function (a, b) {";  // Missing name and closing
           let result = rust_parse_function(source);
           assert!(!result.success);
           assert!(!result.error_message.is_empty());
       }
   }
   ```

2. **ECMAScript Test262**
   ```bash
   # Download test262
   git clone https://github.com/tc39/test262.git
   
   # Run parser tests
   cargo test --release
   
   # Run against test262 suite
   ./run_test262.sh parser
   ```

3. **Performance Benchmarks**
   ```rust
   #[cfg(test)]
   mod benches {
       use super::*;
       use std::time::Instant;
       
       #[test]
       fn bench_parse_simple() {
           let source = "function foo() { return 42; }";
           let iterations = 10000;
           
           let start = Instant::now();
           for _ in 0..iterations {
               let _ = rust_parse_function(source);
           }
           let duration = start.elapsed();
           
           println!("Average: {:?}", duration / iterations);
       }
   }
   ```

4. **Integration with Chromium**
   ```bash
   # Add feature flag
   # In chrome/browser/about_flags.cc
   
   # Build Chromium with Rust parser
   autoninja -C out/Default chrome
   
   # Test with flag
   ./out/Default/chrome --enable-rust-parser
   
   # Run web platform tests
   ./third_party/blink/tools/run_web_tests.py
   ```

---

## Debugging Tips

### Setting Up Debugger
```bash
# For Rust code
export RUST_BACKTRACE=full
rust-gdb ./out/Default/chrome

# Set breakpoints in Rust
(gdb) break rust_v8_parser::parser::Parser::parse_function_literal

# For mixed debugging
(gdb) break v8::internal::Parser::ParseFunctionLiteral
(gdb) break rust_parse_function
```

### Common Issues

1. **Build Errors**
   ```bash
   # Clean build
   rm -rf out/Default
   gn gen out/Default
   autoninja -C out/Default chrome
   ```

2. **FFI Crashes**
   - Check for null pointers
   - Validate lifetimes
   - Use AddressSanitizer
   ```bash
   gn args out/Default
   # Add: is_asan = true
   ```

3. **Performance Issues**
   - Profile with perf
   ```bash
   perf record -g ./out/Default/chrome
   perf report
   ```

---

## Resources

### Documentation
- Full Research: [V8_RUSTIFICATION_RESEARCH.md](./V8_RUSTIFICATION_RESEARCH.md)
- Summary: [V8_RUSTIFICATION_SUMMARY.md](./V8_RUSTIFICATION_SUMMARY.md)
- Chromium Rust: `/docs/rust/`
- V8 Dev: https://v8.dev/

### Community
- Chromium-dev: https://groups.google.com/a/chromium.org/g/chromium-dev
- V8-dev: https://groups.google.com/g/v8-dev
- Rust Users: https://users.rust-lang.org/

### Getting Help
- Platform Architecture: platform-architecture-dev@chromium.org
- V8 Team: v8-dev@googlegroups.com
- Rust Questions: rust-dev@chromium.org

---

## Checklist

### Before Starting
- [ ] Read full research document
- [ ] Understand V8 architecture
- [ ] Set up Chromium development environment
- [ ] Install Rust toolchain
- [ ] Practice FFI with simple examples

### Phase 1: Scanner
- [ ] Define token types
- [ ] Implement scanner
- [ ] Add unit tests
- [ ] Pass all scanner tests

### Phase 2: Parser
- [ ] Define AST types
- [ ] Implement parser for simple functions
- [ ] Add recursive parsing
- [ ] Handle error cases
- [ ] Pass all parser tests

### Phase 3: FFI
- [ ] Define FFI interface
- [ ] Implement C++ adapter
- [ ] Test FFI boundary
- [ ] Validate memory safety

### Phase 4: Integration
- [ ] Integrate with V8
- [ ] Add feature flag
- [ ] Run test262
- [ ] Performance benchmarks
- [ ] Gradual rollout

---

**Good luck with the implementation!** 🦀

For questions or issues, refer to the full documentation or reach out to the teams listed above.
