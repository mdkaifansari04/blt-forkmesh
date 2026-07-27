ALTER TABLE world_build_board_items
  ADD COLUMN title TEXT NOT NULL DEFAULT '' CHECK (length(title) <= 160);
