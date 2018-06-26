CREATE OR REPLACE FUNCTION cloudlyst_update_parent_etag(v_parent_id bigint, v_mtime integer) RETURNS void AS $$
BEGIN
    WHILE v_parent_id IS NOT NULL LOOP
        UPDATE cloudlyst.files SET mtime = v_mtime, etag = to_hex(mtime)||to_hex(id) WHERE id = v_parent_id;
        SELECT parent_id INTO v_parent_id FROM cloudlyst.files WHERE id = v_parent_id FOR UPDATE;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION cloudlyst_files_update_parent_etag() RETURNS trigger AS $$
DECLARE
    v_size_diff bigint;
BEGIN
    IF TG_OP = 'UPDATE' THEN
        IF NEW.parent_id != OLD.parent_id THEN
            UPDATE cloudlyst.files SET size = size - OLD.size, mtime = extract(epoch from now()), etag = to_hex(mtime)||to_hex(id) WHERE id = OLD.parent_id;
            UPDATE cloudlyst.files SET size = size + NEW.size, mtime = extract(epoch from now()), etag = to_hex(mtime)||to_hex(id) WHERE id = NEW.parent_id;
        ELSIF NEW.size != OLD.size THEN
            v_size_diff := NEW.size - OLD.size;
            UPDATE cloudlyst.files SET size = size + v_size_diff, mtime = extract(epoch from now()), etag = to_hex(mtime)||to_hex(id) WHERE id = NEW.parent_id;
        END IF;
    ELSE
        UPDATE cloudlyst.files SET size = size + NEW.size, mtime = extract(epoch from now()), etag = to_hex(mtime)||to_hex(id) WHERE id = NEW.parent_id;
    END IF;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER cloudlyst_files_mtime_update
    AFTER INSERT OR UPDATE ON cloudlyst.files
    FOR EACH ROW
    EXECUTE PROCEDURE cloudlyst_files_update_parent_etag();

CREATE OR REPLACE FUNCTION cloudlyst_files_update_parent_etag_on_delete() RETURNS trigger AS $$
BEGIN
    UPDATE cloudlyst.files SET size = size - OLD.size, mtime = extract(epoch from now()), etag = to_hex(mtime)||to_hex(id) WHERE id = OLD.parent_id;

    RETURN OLD;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER cloudlyst_files_mtime_update_on_delete
    AFTER DELETE ON cloudlyst.files
    FOR EACH ROW
    EXECUTE PROCEDURE cloudlyst_files_update_parent_etag_on_delete();

CREATE OR REPLACE FUNCTION cloudlyst_put(v_path varchar, v_name varchar, v_parent_path varchar, v_mtime integer, v_storage_mtime integer, v_mimetype varchar, v_size bigint, v_etag varchar, v_owner_id integer) RETURNS bigint AS $$
DECLARE
    v_parent_id bigint;
    v_mimetype_id integer;
    v_file_id bigint;
BEGIN
    IF v_parent_path <> '' THEN
        SELECT id INTO v_parent_id FROM cloudlyst.files WHERE path = v_parent_path AND owner_id = v_owner_id FOR UPDATE;
        IF v_parent_id = NULL THEN
            RAISE EXCEPTION 'Nonexistent parent path --> %', v_parent_path;
        END IF;
    ELSE
        SELECT id INTO v_parent_id FROM cloudlyst.files WHERE path = 'files' AND owner_id = v_owner_id FOR UPDATE;
        IF v_parent_id = NULL THEN
            INSERT INTO cloudlyst.files (path, name, mtime, storage_mtime, mimetype_id, size, etag, owner_id, parent_id) VALUES
                ('files', 'files', v_mtime, v_storage_mtime, NULL, 0, NULL, v_owner_id, NULL) RETURNING id INTO v_parent_id;
        END IF;
    END IF;
    
    SELECT id INTO v_mimetype_id FROM cloudlyst.mimetypes WHERE name = v_mimetype;
    IF v_mimetype_id IS NULL THEN
        INSERT INTO cloudlyst.mimetypes (name) VALUES (v_mimetype) RETURNING id INTO v_mimetype_id;
    END IF;
    

    INSERT INTO cloudlyst.files (path, name, mtime, storage_mtime, mimetype_id, size, etag, owner_id, parent_id) VALUES 
        (v_path, v_name, v_mtime, v_storage_mtime, v_mimetype_id, v_size, v_etag, v_owner_id, v_parent_id) 
    ON CONFLICT ON CONSTRAINT files_path_owner_id_key DO UPDATE SET mtime = v_mtime, storage_mtime = v_storage_mtime, mimetype_id = v_mimetype_id, size = v_size, etag = v_etag
    RETURNING id INTO v_file_id;
        
    RETURN v_file_id;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION cloudlyst_copy(v_path varchar, v_dest_parent_path varchar, v_dest_path varchar, v_dest_name varchar, v_owner_id integer) RETURNS bigint AS $$
DECLARE
    v_parent_id bigint;
    v_file_id bigint;
BEGIN
    IF v_dest_parent_path <> '' THEN
        SELECT id INTO v_parent_id FROM cloudlyst.files WHERE path = v_dest_parent_path AND owner_id = v_owner_id FOR UPDATE;
        IF v_parent_id = NULL THEN
            RAISE EXCEPTION 'Nonexistent parent path --> %', v_dest_parent_path;
        END IF;
    ELSE
        SELECT id INTO v_parent_id FROM cloudlyst.files WHERE path = 'files' AND owner_id = v_owner_id FOR UPDATE;
        IF v_parent_id = NULL THEN
            INSERT INTO cloudlyst.files (path, name, mtime, mimetype_id, size, etag, owner_id, parent_id) VALUES
                ('files', 'files', v_mtime, NULL, 0, NULL, v_owner_id, NULL) RETURNING id INTO v_parent_id;
        END IF;
    END IF;

    INSERT INTO cloudlyst.files (path, name, mtime, storage_mtime, mimetype_id, size, etag, owner_id, parent_id)
        (SELECT v_dest_path, v_dest_name, mtime, storage_mtime, mimetype_id, size, etag, v_owner_id, v_parent_id FROM cloudlyst.files WHERE path = v_path) 
        RETURNING id INTO v_file_id;
    
    RETURN v_file_id;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION cloudlyst_move(v_path varchar, v_dest_path varchar, v_dest_name varchar, v_owner_id integer) RETURNS void AS $$
DECLARE
    v_parent_id bigint;
BEGIN
    UPDATE cloudlyst.files SET path = v_dest_path, name = v_dest_name WHERE path = v_path AND owner_id = v_owner_id
        RETURNING parent_id INTO v_parent_id;
    
    UPDATE cloudlyst.files SET path = overlay(path placing v_dest_path||'/' from 1 for length(v_path) + 1) WHERE path LIKE v_path||'/%' AND owner_id = v_owner_id
        RETURNING parent_id INTO v_parent_id;
END;
$$ LANGUAGE plpgsql;
