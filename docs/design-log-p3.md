# Project 3 Design Log — Draft for Student Review

**Author:** Kesav Lakshminarayanan
**Project 2 code:** The student's own `Conversation` and `SentinelScanner` files, built with `USE_P2_REFERENCE=OFF`; the instructor reference was not selected for final verification.  
**Tools and assistance:** CMake/CTest, GCC, AddressSanitizer, UndefinedBehaviorSanitizer, LeakSanitizer, command-line file comparisons, and Python for verification. OpenAI ChatGPT assisted with specification verification**

## 1. Hash table and measurements

`ToolRegistry` uses separate chaining. Its table begins with 32 bucket-head pointers, each initialized to null. A bucket holds a singly linked list of nodes containing an owned `std::string` key, a `std::unique_ptr<Tool>`, and the next-node pointer. When two names map to the same bucket, lookup compares keys along that chain. Chaining avoids tombstones and complex probe termination; tools are not individually removed.

The hash iterates over every byte in a name, xors it into a 64-bit accumulator, and multiplies by the constant from the specification's FNV-1a example. This is order-sensitive: `"ab"` and `"ba"` receive different hashes. Only after producing the full hash does the table use modulo the bucket count to choose a bucket. It is not a cryptographic hash.

Before an insertion would exceed a 0.75 load-factor threshold, the bucket count doubles. Rehashing allocates the replacement array first, then relinks existing nodes into their new buckets. Insertion stages its node before resizing, protecting old entries from allocation failure. Growth also checks for overflow. With bounded key lengths and a good distribution, lookup is expected O(1). Geometric growth distributes occasional O(n) rehash costs across many insertions, so `add` is amortized O(1).

The final executable's 10,000-name test measured **16,384 buckets**, **0.6103515625 load factor**, and **maximum chain length 6**. All 10,000 names were retrievable, all recorded `Tool*` addresses were unchanged, and the hash yielded **10,000 distinct full-width values**. At that same bucket count, the weak hash `s[0] + s.size()` produced a **maximum chain length of 9,000**. The weak hash groups same-length names starting with `t`; the implemented hash distributes them more evenly. These are not worst-case guarantees.

## 2. Pointer stability and ownership

The registry owns every accepted tool through a `unique_ptr`. Its `find` method returns only a borrowed pointer, so callers must not delete the tool. During growth, rehashing changes node links without constructing, copying, or moving any underlying `Tool` object. Therefore, a pointer obtained before resizing remains valid until the registry is destroyed. A duplicate name is rejected before insertion; the incoming tool is destroyed while the original remains registered. Destruction walks the chains and deletes each node, which destroys its tool exactly once.

## 3. Calculator two-stack trace

For `2 * (3 + 4) - 5`, the value stack first receives `2`, while the operator stack receives `*` followed by `(`. Pushing `3`, `+`, and `4` leaves values `[2, 3, 4]` and operators `[*, (, +]`. At `)`, the evaluator pops `4` as the right operand and `3` as the left, applies `+`, and removes `(`. Values become `[2, 7]` and operators `[ * ]`. The incoming `-` has lower precedence, so `2 * 7` is applied before `-` is pushed. Finally, `5` is pushed; the end-of-input step applies `14 - 5`, leaving the single result `9`. The algorithm uses loops and checks stack sizes before popping. Equal-precedence operators are applied first to preserve left associativity.

## 4. Errors as values

A failed file access, unsafe path, or division by zero is an ordinary result the agent can correct. File tools return `ToolResult::failure`, while `resolve` and `calc_evaluate` return `false` and provide explanations. This avoids breaking the tool loop for expected user-input errors. Programmer mistakes and exceptional resource failures are different: an allocation may throw, and the dispatcher provides a final exception boundary. The resolver attempts to contain path-operation exceptions, although a nonempty `std::string` diagnostic cannot be guaranteed during total memory exhaustion.

## 5. Design reflection — student to personalize

An alternative is a vector of standard-library lists to simplify cleanup through RAII, with careful node splicing during rehash to preserve pointers. The current representation is small, explicit, and meets the measured requirements. **Student review required:** replace this proposed tradeoff with the design decision and reasoning you can personally explain during the oral check-in.
