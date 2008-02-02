define noit_hash_show
  set $ht = $arg0
  set $nb = 0

  while $nb < $ht->table_size
    set $b = $ht->buckets[$nb]
    while $b != 0
      printf "[%d] \"%s\" => %p\n", $nb, $b->k, $b->data
      set $b = $b->next
    end

    set $nb = $nb + 1
  end
end

document noit_hash_show
  Shows the contents of an noit_hash; displays
  the bucket number, key and data pointer
end

define noit_hash_showstr
  set $ht = $arg0
  set $nb = 0

  while $nb < $ht->table_size
    set $b = $ht->buckets[$nb]
    while $b != 0
      printf "[%d] \"%s\" => %s\n", $nb, $b->k, (char *)$b->data
      set $b = $b->next
    end

    set $nb = $nb + 1
  end
end

handle SIGPIPE nostop
handle SIG32 nostop

# vim:ts=2:sw=2:et:
