# Database Migrations

This directory contains versioned startup migrations.

## Conventions

- One file per migration version.
- File name format: `V<N>_<ShortDescription>.hpp`.
- Each file exposes one `createV<N>...Migration()` function.
- Register all migrations in `../Migrations.hpp`.
- Keep schema changes non-destructive:
  - prefer `CREATE TABLE IF NOT EXISTS`
  - prefer `INSERT IGNORE`
  - avoid `DROP`, `TRUNCATE`, and unguarded `DELETE`
- Wrap seed/data migrations in `runTransactionalMigration(...)` so a failure rolls back cleanly.

## Current versions

- `V1_CreateBaseSchema.hpp`
- `V2_SeedDefaultAdmin.hpp`
- `V3_SeedDefaultMenus.hpp`
