import { oneLine, truncate, visibleLength } from "./format";

export interface Column<T> {
  header: string;
  /** Plain text for the cell. It is sanitised and squashed onto one line. */
  value: (row: T) => string;
  align?: "left" | "right";
  /** Styling applied after layout, so escape codes never affect widths. */
  style?: (text: string, row: T) => string;
  /** May be shortened (with an ellipsis) when the table is wider than `maxWidth`. */
  flex?: boolean;
  /** Narrowest a flex column will be squeezed to. Default 10. */
  minWidth?: number;
}

export interface TableOptions {
  /** Shrink flex columns so no line exceeds this. Without it nothing is ever truncated. */
  maxWidth?: number;
  /** Spaces between columns. Default 2. */
  gap?: number;
  indent?: string;
  headerStyle?: (text: string) => string;
}

/** Renders rows as aligned plain-text columns and returns the lines (header first, no trailing spaces). */
export function renderTable<T>(columns: Array<Column<T>>, rows: T[], options: TableOptions = {}): string[] {
  const gap = " ".repeat(options.gap ?? 2);
  const indent = options.indent ?? "";
  const headerStyle = options.headerStyle ?? ((text: string) => text);

  const cells = rows.map((row) => columns.map((column) => oneLine(column.value(row))));
  const widths = columns.map((column, index) => Math.max(visibleLength(column.header), ...cells.map((line) => visibleLength(line[index]))));

  if (options.maxWidth !== undefined) {
    const total = () => indent.length + widths.reduce((sum, w) => sum + w, 0) + gap.length * (columns.length - 1);
    const floor = (index: number) => Math.max(columns[index].minWidth ?? 10, visibleLength(columns[index].header));
    while (total() > options.maxWidth) {
      let widest = -1;
      columns.forEach((column, index) => {
        if (column.flex && widths[index] > floor(index) && (widest === -1 || widths[index] > widths[widest])) widest = index;
      });
      if (widest === -1) break;
      widths[widest] -= 1;
    }
  }

  const pad = (text: string, width: number, align: "left" | "right" | undefined): string => {
    const gapWidth = Math.max(0, width - visibleLength(text));
    return align === "right" ? " ".repeat(gapWidth) + text : text + " ".repeat(gapWidth);
  };

  const line = (parts: string[]) => `${indent}${parts.join(gap)}`.replace(/\s+$/, "");

  // `pad` measures visible width, so styled (ANSI-wrapped) text lines up like plain text.
  const lines = [line(columns.map((column, index) => pad(headerStyle(column.header), widths[index], column.align)))];
  rows.forEach((row, rowIndex) => {
    lines.push(
      line(
        columns.map((column, index) => {
          const shown = truncate(cells[rowIndex][index], widths[index]);
          const styled = column.style ? column.style(shown, row) : shown;
          return pad(styled, widths[index], column.align);
        })
      )
    );
  });
  return lines;
}
