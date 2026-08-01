








CREATE TABLE IF NOT EXISTS world_visit_unique_hll (
    bucket_start INTEGER NOT NULL,
    register_id INTEGER NOT NULL
        CHECK (register_id >= 0 AND register_id < 1024),
    rank INTEGER NOT NULL CHECK (rank >= 1 AND rank <= 247),
    PRIMARY KEY (bucket_start, register_id)
) WITHOUT ROWID;
