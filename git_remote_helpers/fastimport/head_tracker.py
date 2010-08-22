

class HeadTracker(object):
    """
    Keep track of the heads in a fastimport stream.
    """
    def __init__(self):
        self.last_ref = None

        # map git ref name (e.g. "refs/heads/master") to id of last
        # commit with that ref
        self.last_ids = {}

        # the set of heads seen so far in the stream, as a mapping
        # from commit id of the head to set of ref names
        self.heads = {}

    def track_heads(self, cmd):
        """Track the repository heads given a CommitCommand.
        
        :param cmd: the CommitCommand
        :return: the list of parents in terms of commit-ids
        """
        # Get the true set of parents
        if cmd.from_ is not None:
            parents = [cmd.from_]
        else:
            last_id = self.last_ids.get(cmd.ref)
            if last_id is not None:
                parents = [last_id]
            else:
                parents = []
        parents.extend(cmd.merges)

        # Track the heads
        self.track_heads_for_ref(cmd.ref, cmd.id, parents)
        return parents

    def track_heads_for_ref(self, cmd_ref, cmd_id, parents=None):
        if parents is not None:
            for parent in parents:
                if parent in self.heads:
                    del self.heads[parent]
        self.heads.setdefault(cmd_id, set()).add(cmd_ref)
        self.last_ids[cmd_ref] = cmd_id
        self.last_ref = cmd_ref
    
