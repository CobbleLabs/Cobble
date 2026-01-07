-- Migration: Remove Discord integration
-- Run with: psql $DATABASE_URL -f migrations/004_remove_discord.sql

BEGIN;

-- Remove discord_id column from users table
ALTER TABLE users DROP COLUMN IF EXISTS discord_id;

COMMIT;
