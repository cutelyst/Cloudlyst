CREATE OR REPLACE FUNCTION cloudlyst_put(v_path varchar, v_name varchar, v_parent_path varchar, v_mtime integer, v_mimetype varchar, v_size bigint, v_etag varchar, v_owner_id integer) RETURNS bigint AS $$
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
            INSERT INTO cloudlyst.files (path, name, mtime, mimetype_id, size, etag, owner_id, parent_id) VALUES
                ('files', 'files', v_mtime, NULL, 0, NULL, v_owner_id, NULL) RETURNING id INTO v_parent_id;
        END IF;
    END IF;
    
    SELECT id INTO v_mimetype_id FROM cloudlyst.mimetypes WHERE name = v_mimetype;
    IF v_mimetype_id IS NULL THEN
        INSERT INTO cloudlyst.mimetypes (name) VALUES (v_mimetype) RETURNING id INTO v_mimetype_id;
    END IF;
    

    INSERT INTO cloudlyst.files (path, name, mtime, mimetype_id, size, etag, owner_id, parent_id) VALUES 
        (v_path, v_name, v_mtime, v_mimetype_id, v_size, v_etag, v_owner_id, v_parent_id) 
    ON CONFLICT ON CONSTRAINT files_path_owner_id_key DO UPDATE SET mtime=v_mtime, mimetype_id = v_mimetype_id, size=v_size, etag=v_etag
    RETURNING id INTO v_file_id;
        
    WHILE v_parent_id IS NOT NULL LOOP
        UPDATE cloudlyst.files SET mtime = v_mtime WHERE id = v_parent_id;
        SELECT parent_id INTO v_parent_id FROM cloudlyst.files WHERE id = v_parent_id FOR UPDATE;
    END LOOP;
    
    RETURN v_file_id;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION cloudlyst_copy(v_path varchar, v_dest_parent_path varchar, v_dest_path varchar, v_dest_name varchar, v_owner_id integer) RETURNS bigint AS $$
DECLARE
    v_parent_id bigint;
    v_mimetype_id integer;
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
            INSERT INTO cloudlyst.files (path, name, mtime, mimetype, size, etag, owner_id, parent_id) VALUES
                ('files', 'files', v_mtime, NULL, 0, NULL, v_owner_id, NULL) RETURNING id INTO v_parent_id;
        END IF;
    END IF; 

    INSERT INTO cloudlyst.files (path, name, mtime, mimetype, size, etag, owner_id, parent_id) VALUES 
        (v_dest_path, v_dest_name, (SELECT mtime, mimetype_id, size, etag FROM cloudlyst.files WHERE path = v_path), v_owner_id, v_parent_id) 
    ON CONFLICT ON CONSTRAINT files_path_owner_id_key DO UPDATE SET mtime=v_mtime, mimetype=v_mimetype_id, size=v_size, etag=v_etag
    RETURNING id INTO v_file_id;
        
    WHILE v_parent_id IS NOT NULL LOOP
        UPDATE cloudlyst.files SET mtime = v_mtime WHERE id = v_parent_id;
        SELECT parent_id INTO v_parent_id FROM cloudlyst.files WHERE id = v_parent_id FOR UPDATE;
    END LOOP;
    
    RETURN v_file_id;
END;
$$ LANGUAGE plpgsql;
